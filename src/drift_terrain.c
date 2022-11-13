#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LIFFT_IMPLEMENTATION
#define LIFFT_FLOAT_TYPE float
#include "lifft/lifft.h"
#include "tracy/TracyC.h"

#include "drift_game.h"

static uint mip_base(uint level){return (DRIFT_TERRAIN_TILEMAP_SIZE_SQ/3)>>(2*level);}

static inline uint tile_index(DriftTerrain* terra, DriftTerrainTileCoord coord){
	uint max = DRIFT_TERRAIN_TILEMAP_SIZE >> coord.level;
	if(coord.x < max && coord.y < max){
		return mip_base(coord.level) + coord.x + coord.y*(DRIFT_TERRAIN_TILEMAP_SIZE >> coord.level);
	} else {
		return 0;
	}
}

static const float Q_VALUES[64] = {9704.0, 2198.0, 2237.0, 1883.0, 1796.0, 1241.0, 1124.0, 817.0, 657.0, 599.0, 522.0, 456.0, 501.0, 539.0, 498.0, 695.0, 501.0, 529.0, 491.0, 595.0, 565.0, 588.0, 623.0, 844.0, 844.0, 908.0, 867.0, 929.0, 992.0, 1290.0, 1430.0, 1374.0, 1262.0, 1269.0, 1136.0, 1258.0, 1019.0, 1213.0, 1046.0, 857.0, 897.0, 972.0, 889.0, 789.0, 753.0, 863.0, 900.0, 827.0, 623.0, 702.0, 707.0, 656.0, 624.0, 724.0, 653.0, 744.0, 585.0, 656.0, 659.0, 622.0, 960.0, 777.0, 697.0};

static void encode_tile(tina_job* job){
	DriftTerrainDensity* density_base = tina_job_get_description(job)->user_data;
	uint idx = tina_job_get_description(job)->user_idx;
	uint tx = idx % DRIFT_TERRAIN_TILEMAP_SIZE, ty = idx / DRIFT_TERRAIN_TILEMAP_SIZE;
	
	u8* samples = density_base[idx].samples;
	float values[DRIFT_TERRAIN_TILE_SIZE_SQ];
	for(uint i = 0; i < DRIFT_TERRAIN_TILE_SIZE_SQ; i++) values[i] = samples[i];
	LIFFT_APPLY_2D(lifft_forward_dct, values, values, DRIFT_TERRAIN_TILE_SIZE);
	
	for(uint i = 0; i < DRIFT_TERRAIN_TILE_SIZE_SQ; i++){
		uint x = i % DRIFT_TERRAIN_TILE_SIZE, y = i / DRIFT_TERRAIN_TILE_SIZE;
		float q = Q_VALUES[x + y];
		float quant = roundf(values[i]/q);
		DRIFT_ASSERT(quant == (s8)quant, "Value out of range (%d %d)[%d, %d] -> %f / %f", tx, ty, x, y, values[i], q);
		samples[i] = (s8)quant;
	}
}

#define CHUNK_SPLITS 16
static DriftTerrainDensity BASE_TERRAIN[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
static tina_group BASE_TERRAIN_GROUP = {};

static void decode_tiles(tina_job* job){
	TracyCZoneN(ZONE_DECODE, "Decode Tiles", true);
	DriftTerrainDensity* density_dst = tina_job_get_description(job)->user_data;
	uint idx0 = tina_job_get_description(job)->user_idx;
	
	uint batch_size = DRIFT_TERRAIN_TILEMAP_SIZE_SQ/DRIFT_TERRAIN_FILE_CHUNKS/CHUNK_SPLITS;
	for(uint i = 0; i < batch_size; i++){
		uint idx = idx0*batch_size + i;
		uint tx = idx % DRIFT_TERRAIN_TILEMAP_SIZE, ty = idx / DRIFT_TERRAIN_TILEMAP_SIZE;
		
		TracyCZoneN(ZONE_QUANT, "Quant", true);
		u8* samples = density_dst[idx].samples;
		float values[DRIFT_TERRAIN_TILE_SIZE_SQ];
		for(uint i = 0; i < DRIFT_TERRAIN_TILE_SIZE_SQ; i++){
			uint x = i % DRIFT_TERRAIN_TILE_SIZE, y = i / DRIFT_TERRAIN_TILE_SIZE;
			float q = Q_VALUES[x + y];
			values[i] = (s8)samples[i]*q;
		}
		TracyCZoneEnd(ZONE_QUANT);
		
		TracyCZoneN(ZONE_DCT, "DCT", true);
		uint n = DRIFT_TERRAIN_TILE_SIZE;
		float tmp[n*n];
		for(uint i = 0; i < n; i++) lifft_inverse_dct(values + i*n, 1, tmp + i, n, n);
		for(uint i = 0; i < n; i++) lifft_inverse_dct(tmp + i*n, 1, values + i, n, n);
		TracyCZoneEnd(ZONE_DCT);
		
		for(uint i = 0; i < DRIFT_TERRAIN_TILE_SIZE_SQ; i++) samples[i] = (u8)DriftClamp(values[i], 0, 255);
	}
	TracyCZoneEnd(ZONE_DECODE);
}

static void load_chunk(tina_job* job){
	TracyCZoneN(ZONE_LOAD, "Load Terrain", true);
	uint idx = tina_job_get_description(job)->user_idx;
	
	DriftData data = DriftAssetLoad(DriftSystemMem, "bin/terrain%d.bin", idx);
	
	void* density_base = tina_job_get_description(job)->user_data;
	void* dst = density_base + idx*data.size;
	memcpy(dst, data.ptr, data.size);
	DriftDealloc(DriftSystemMem, data.ptr, data.size);
	TracyCZoneEnd(ZONE_LOAD);
	
	tina_scheduler_enqueue_n(tina_job_get_scheduler(job), decode_tiles, dst, CHUNK_SPLITS, DRIFT_JOB_QUEUE_WORK, &BASE_TERRAIN_GROUP);
}

void DriftTerrainLoadBase(tina_scheduler* sched){
	tina_scheduler_enqueue_n(sched, load_chunk, BASE_TERRAIN, DRIFT_TERRAIN_FILE_CHUNKS, DRIFT_JOB_QUEUE_WORK, &BASE_TERRAIN_GROUP);
}

void DriftTerrainResetCache(DriftTerrain* terra){
	for(uint i = 0; i < DRIFT_TERRAIN_TILECACHE_SIZE; i++){
		terra->cache_heap[i] = (DriftTerrainCacheEntry){.texture_idx = i, .tile_idx = 0};
	}
	
	for(uint i = 0; i < DRIFT_TERRAIN_TILE_COUNT; i++){
		terra->tilemap.state[i] = i < DRIFT_TERRAIN_MIP0 ? DRIFT_TERRAIN_TILE_STATE_DIRTY : DRIFT_TERRAIN_TILE_STATE_READY;
		terra->tilemap.timestamps[i] = 0;
		terra->tilemap.texture_idx[i] = 0;
	}
	
	terra->biome_dirty = true;
}

DriftTerrain* DriftTerrainNew(tina_job* job, bool force_regen){
	TracyCZoneN(ZONE_TERRAIN, "Terrain New", true);
	DriftTerrain* terra = DriftAlloc(DriftSystemMem, sizeof(*terra));
	memset(terra, 0, sizeof(*terra));
	
	float tile_size = DRIFT_TERRAIN_TILE_SCALE*DRIFT_TERRAIN_TILE_SIZE;
	float map_size = tile_size*DRIFT_TERRAIN_TILEMAP_SIZE;
	terra->map_to_world = (DriftAffine){
		tile_size, 0, 0, tile_size,
		-map_size/2, -map_size/2,
	};
	terra->world_to_map = DriftAffineInverse(terra->map_to_world);
	
	for(uint level = 0; level <= DRIFT_TERRAIN_TILEMAP_SIZE_LOG; level++){
		DriftTerrainTileCoord* coords = terra->tilemap.coord + mip_base(level);
		uint size = DRIFT_TERRAIN_TILEMAP_SIZE >> level;
		for(uint i = 0; i < size*size; i++){
			DriftTerrainTileCoord c = {i%size, i/size, level};
			coords[i] = c;
			uint idx = tile_index(terra, c);
			DRIFT_ASSERT(idx == (coords + i - terra->tilemap.coord), "wha");
		}
	}
	
	// TODO should load the visibility map instead
	memset(terra->tilemap.visibility, 0x00, sizeof(terra->tilemap.visibility));
	terra->visibility_dirty = true;
	
	DriftTerrainDensity* density_base = terra->tilemap.density + DRIFT_TERRAIN_MIP0;
	tina_job_wait(job, &BASE_TERRAIN_GROUP, 0);
	memcpy(density_base, BASE_TERRAIN, sizeof(BASE_TERRAIN));
	DriftTerrainResetCache(terra);
	
	DriftData data = DriftAssetLoad(DriftSystemMem, "bin/biome.bin");
	memcpy(terra->tilemap.biome, data.ptr, data.size);
	DriftDealloc(DriftSystemMem, (void*)data.ptr, data.size);
	TracyCZoneEnd(ZONE_TERRAIN);
	
	return terra;
}

void DriftTerrainFree(DriftTerrain* terra){
	for(uint idx = 0; idx < DRIFT_TERRAIN_TILE_COUNT; idx++){
		DriftArrayFree(terra->tilemap.collision[idx].segments);
	}
	
	DriftDealloc(DriftSystemMem, terra, sizeof(*terra));
}

static void gather_mip(DriftTerrain* terra, uint idx);

static void gather_tile_row(DriftTerrain* terra, uint idx0, uint idx1, uint offset, u32* rw_buffer){
	u16 sample = 0;
	if(idx0 != ~0u){
		gather_mip(terra, idx0);
		sample = terra->tilemap.density[idx0].samples[offset + DRIFT_TERRAIN_TILE_SIZE - 1];
	}
	
	gather_mip(terra, idx1);
	for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++){
		sample = (sample << 8) | terra->tilemap.density[idx1].samples[offset + x];
		rw_buffer[x] = (rw_buffer[x] << 16) | sample;
	}
}

// 0xAABBCCDD encodes the square of pixels:
// C D
// A B

static void gather_tile_density(DriftTerrain* terra, uint idx, u32* density_texel_buffer){
	u32 row[DRIFT_TERRAIN_TILE_SIZE] = {};
	DriftTerrainTileCoord coord = terra->tilemap.coord[idx];
	DriftTerrainDensity* density = terra->tilemap.density;
	
	if(coord.y > 0){
		size_t offset = (DRIFT_TERRAIN_TILE_SIZE - 1)*DRIFT_TERRAIN_TILE_SIZE;
		uint idx01 = tile_index(terra, (DriftTerrainTileCoord){coord.x - 0, coord.y - 1, coord.level});
		uint idx11 = tile_index(terra, (DriftTerrainTileCoord){coord.x - 1, coord.y - 1, coord.level});
		gather_tile_row(terra, idx11, idx01, offset, row);
	}
	
	for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
		size_t offset = y*DRIFT_TERRAIN_TILE_SIZE;
		uint idx10 = tile_index(terra, (DriftTerrainTileCoord){coord.x - 1, coord.y - 0, coord.level});
		gather_tile_row(terra, idx10, idx, offset, row);
		memcpy(density_texel_buffer + offset, row, sizeof(row));
	}
}

static void gather_sub(DriftTerrain* terra, uint dst_idx, uint x, uint y){
	DriftTerrainTileCoord c = terra->tilemap.coord[dst_idx];
	c = (DriftTerrainTileCoord){2*c.x + x, 2*c.y + y, c.level - 1};
	uint src_idx = tile_index(terra, c);
	gather_mip(terra, src_idx);
	
	DriftTerrainDensity* density = terra->tilemap.density;
	u8* dst = density[dst_idx].samples + (x*DRIFT_TERRAIN_TILE_SIZE + y*DRIFT_TERRAIN_TILE_SIZE_SQ)/2;
	u8* src = density[src_idx].samples;
	for(uint i = 0; i < DRIFT_TERRAIN_TILE_SIZE_SQ/4; i++){
		uint idx = (i & 0x0F) | ((i & 0xF0) << 1);
		dst[idx] = (0
			+ src[2*idx + 0 + 0*DRIFT_TERRAIN_TILE_SIZE]
			+ src[2*idx + 1 + 0*DRIFT_TERRAIN_TILE_SIZE]
			+ src[2*idx + 0 + 1*DRIFT_TERRAIN_TILE_SIZE]
			+ src[2*idx + 1 + 1*DRIFT_TERRAIN_TILE_SIZE]
		)/4;
	}
}

static void gather_mip(DriftTerrain* terra, uint idx){
	if(terra->tilemap.state[idx] == DRIFT_TERRAIN_TILE_STATE_DIRTY){
		DRIFT_ASSERT(terra->tilemap.coord[idx].level > 0, "Cannot gather mips for a level 0 tile.");
		gather_sub(terra, idx, 0, 0);
		gather_sub(terra, idx, 1, 0);
		gather_sub(terra, idx, 0, 1);
		gather_sub(terra, idx, 1, 1);
		terra->tilemap.state[idx] = DRIFT_TERRAIN_TILE_STATE_READY;
	}
}

// TODO Magic number for subpixel bits?
// TODO Is the winding on this right?
static inline uint mid(u8 a, u8 b, u8 t){return 8*(t - a)/(b - a);}
static inline DriftSegment seg(uint x, uint y, uint x0, uint y0, uint x1, uint y1){
	return (DriftSegment){{8*x + x1, 8*y + y1}, {8*x + x0, 8*y + y0}};
}

static void update_tile_shadows(DriftTerrain* terra, uint tile_idx, u32* gathered_density){
	// DRIFT_ASSERT(terra->tilemap.state[tile_idx] == DRIFT_TERRAIN_TILE_STATE_READY, "Unexpected tile state for update_tile_shadows()");
	
	DriftTerrainTileCoord coord = terra->tilemap.coord[tile_idx];
	float scale = DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILE_SCALE;
	DriftAffine tile_to_map = {1/scale, 0, 0, 1/scale, coord.x, coord.y};
	DriftAffine tile_to_world = DriftAffineMul(terra->map_to_world, tile_to_map);

	const u8 threshold = 128;
	// We need '-threshold' at 6 bits of precision, promoted to 4x7 bits.
	const u32 neg_threshold_4x7 = (-threshold/4 & 0x7F)*0x01010101;
	
	DRIFT_ARRAY(DriftSegment) segments = DRIFT_ARRAY_NEW(DriftSystemMem, 2048, DriftSegment);
	for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
		// Allocate for the worse case scenario, 2 segments per cell. 
		DriftSegment* cursor = DRIFT_ARRAY_RANGE(segments, 2*DRIFT_TERRAIN_TILE_SIZE);
		
		for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++){
			// Grab pre-gathered density values in 0xAABBCCDD order and reduce to 6 bits.
			u32 density_4x6 = gathered_density[x + y*DRIFT_TERRAIN_TILE_SIZE]/4 & 0x3F3F3F3F;
			// Subtract the threshold values and mask out sign bits.
			// 7 bit signed value is big enough to hold a 6 bit difference. Bit 8 is overflow and is discarded.
			u32 sign_bits = (density_4x6 + neg_threshold_4x7) & 0x40404040;
			// If all values were above or below the threshold, the cell is empty.
			if(sign_bits == 0x00000000 || sign_bits == 0x40404040) continue;
			
			// Decode density values. (lowest bits already masked)
			u8 a = (density_4x6 >> 0x00);
			u8 b = (density_4x6 >> 0x08);
			u8 c = (density_4x6 >> 0x10);
			u8 d = (density_4x6 >> 0x18);
			u8 t = threshold/4;
			
			// Collect sign bits into upper 4 bits, then shift down. (DCBA order)
			u8 selector = (sign_bits * 0x408102) >> 28;
			// Finally, output geometry for this cell.
			switch(selector){
				case 0x0: break; // No geometry.
				case 0x1: *cursor++ = seg(x, y, 8, mid(c, a, t), mid(b, a, t), 8); break;
				case 0x2: *cursor++ = seg(x, y, mid(b, a, t), 8, 0, mid(d, b, t)); break;
				case 0x3: *cursor++ = seg(x, y, 8, mid(c, a, t), 0, mid(d, b, t)); break;
				case 0x4: *cursor++ = seg(x, y, mid(d, c, t), 0, 8, mid(c, a, t)); break;
				case 0x5: *cursor++ = seg(x, y, mid(d, c, t), 0, mid(b, a, t), 8); break;
				case 0x6: break; // TODO double corner case.
				case 0x7: *cursor++ = seg(x, y, mid(d, c, t), 0, 0, mid(d, b, t)); break;
				case 0x8: *cursor++ = seg(x, y, 0, mid(d, b, t), mid(d, c, t), 0); break;
				case 0x9: break; // TODO double corner case.
				case 0xA: *cursor++ = seg(x, y, mid(b, a, t), 8, mid(d, c, t), 0); break;
				case 0xB: *cursor++ = seg(x, y, 8, mid(c, a, t), mid(d, c, t), 0); break;
				case 0xC: *cursor++ = seg(x, y, 0, mid(d, b, t), 8, mid(c, a, t)); break;
				case 0xD: *cursor++ = seg(x, y, 0, mid(d, b, t), mid(b, a, t), 8); break;
				case 0xE: *cursor++ = seg(x, y, mid(b, a, t), 8, 8, mid(c, a, t)); break;
				case 0xF: break; // No geometry.
			}
		}
		
		DriftArrayRangeCommit(segments, cursor);
	}
	
	// TODO Apply douglas-peuker? Meh...
	
	// Transform to final coordinates.
	DRIFT_ARRAY_FOREACH(segments, seg){
		seg->a = DriftAffinePoint(tile_to_world, seg->a);
		seg->b = DriftAffinePoint(tile_to_world, seg->b);
	}
	
	DriftArrayFree(terra->tilemap.collision[tile_idx].segments);
	terra->tilemap.collision[tile_idx].segments = segments;
	terra->tilemap.collision[tile_idx].count = DriftArrayLength(segments);
	terra->tilemap.state[tile_idx] = DRIFT_TERRAIN_TILE_STATE_SHADOWS;
}

typedef struct UploadTile {
	uint texture_idx;
	u32 texels[DRIFT_TERRAIN_TILE_SIZE_SQ];
	const struct UploadTile* next;
} UploadTile;

typedef struct {
	DriftDrawShared* draw_shared;
	const UploadTile* tiles;
	DriftRGBA8* biome_tex;
	DriftRGBA8* visibility_tex;
	DriftMem* mem;
} UploadTilesContext;

static void upload_tiles(tina_job* job){
	UploadTilesContext* ctx = tina_job_get_description(job)->user_data;
	const DriftGfxDriver* driver = ctx->draw_shared->driver;
	
	for(const UploadTile* tile = ctx->tiles; tile; tile = tile->next){
		driver->load_texture_layer(driver, ctx->draw_shared->terrain_tiles, tile->texture_idx, tile->texels);
	}
	
	if(ctx->biome_tex) driver->load_texture_layer(driver, ctx->draw_shared->atlas_texture, DRIFT_ATLAS_BIOME, ctx->biome_tex);
	if(ctx->visibility_tex) driver->load_texture_layer(driver, ctx->draw_shared->atlas_texture, DRIFT_ATLAS_VISIBILITY, ctx->visibility_tex);
	DriftZoneMemRelease(ctx->mem);
}

// Update a binary heap entry relative to it's parents.
static void update_cache_entry(DriftTerrainCacheEntry* heap, DriftTerrainCacheEntry *entry, DriftTerrainCacheEntry value){
	DRIFT_ASSERT(entry - heap < DRIFT_TERRAIN_TILECACHE_SIZE, "Bad cache entry");
	DRIFT_ASSERT(entry->timestamp <= value.timestamp, "Backwards timestamp");
	
	size_t idx = entry - heap;
	if(idx < DRIFT_TERRAIN_TILECACHE_SIZE/2 - 1){
		uint i_left = 2*idx + 1, i_right = 2*idx + 2;
		DriftTerrainCacheEntry* parent = heap[i_left].timestamp < heap[i_right].timestamp ? heap + i_left : heap + i_right;
		
		// Compare with oldest parent and swap if necessary.
		if(value.timestamp > parent->timestamp){
			*entry = *parent;
			return update_cache_entry(heap, parent, value);
		}
	}
	
	*entry = value;
}

static DriftTerrainCacheEntry* least_recently_used(DriftTerrain* terra){
	DriftTerrainCacheEntry* entry = terra->cache_heap;
	u64 timestamp = terra->tilemap.timestamps[entry->tile_idx];
	if(entry->timestamp == timestamp){
		return entry;
	} else {
		entry->timestamp = terra->tilemap.timestamps[entry->tile_idx];
		update_cache_entry(terra->cache_heap, entry, *entry);
		return least_recently_used(terra);
	}
}

static void cache_tile(DriftTerrain* terra, uint idx, UploadTilesContext* upload_ctx){
	u64 timestamp = terra->timestamp;
	
	// Mark the tile as recently used.
	terra->tilemap.timestamps[idx] = timestamp;
	
	DriftTerrainTileState state = terra->tilemap.state[idx];
	
	// Nothing to do if it's already cached.
	if(state == DRIFT_TERRAIN_TILE_STATE_CACHED) return;
	if(state == DRIFT_TERRAIN_TILE_STATE_DIRTY) gather_mip(terra, idx);
	
	TracyCZoneN(ZONE_GATHER, "Gather/Shadow", true);
	UploadTile* upload = DriftAlloc(upload_ctx->mem, sizeof(*upload));
	gather_tile_density(terra, idx, upload->texels);
	update_tile_shadows(terra, idx, upload->texels);
	TracyCZoneEnd(ZONE_GATHER);
	
	uint texture_idx = terra->tilemap.texture_idx[idx];
	if(!texture_idx){
		DriftTerrainCacheEntry* entry = least_recently_used(terra);
		texture_idx = entry->texture_idx;
		
		// Evict the old tile using the entry.
		if(entry->tile_idx != 0){
			DRIFT_ASSERT(entry->timestamp != timestamp, "In use tile evicted?! %d", entry->texture_idx);
			
#if DRIFT_DEBUG
			static u64 check;
			DRIFT_ASSERT(entry->timestamp >= check, "Cache error. Current entry is older than the last one.");
			check = entry->timestamp;
#endif
			
			terra->tilemap.texture_idx[entry->tile_idx] = 0;
			terra->tilemap.state[entry->tile_idx] = DRIFT_TERRAIN_TILE_STATE_READY;
		}
		
		update_cache_entry(terra->cache_heap, entry, (DriftTerrainCacheEntry){
			.timestamp = timestamp, .tile_idx = idx, .texture_idx = texture_idx
		});
	}
	
	terra->tilemap.texture_idx[idx] = texture_idx;
	terra->tilemap.state[idx] = DRIFT_TERRAIN_TILE_STATE_CACHED;
	
	upload->texture_idx = texture_idx;
	upload->next = upload_ctx->tiles;
	upload_ctx->tiles = upload;
}

// TODO Only used in one place, can it be inlined/simplified?
// Gather indexes of tiles visible given the VP matrix.
static DRIFT_ARRAY(uint) DriftTerrainVisibleTiles(DriftTerrain* terra, DriftDraw* draw){
	DriftAffine vp_inv = draw->vp_inverse;
	float hw = fabsf(vp_inv.a) + fabsf(vp_inv.c), hh = fabsf(vp_inv.b) + fabsf(vp_inv.d);
	DriftAABB2 bounds = {vp_inv.x - hw, vp_inv.y - hh, vp_inv.x + hw, vp_inv.y + hh};
	
	DriftAffine vp = draw->v_matrix;
	float scale = 2/(sqrtf(vp.a*vp.a + vp.b*vp.b) + sqrtf(vp.c*vp.c + vp.d*vp.d));
	float mip_level = DriftClamp(log2f(scale) - 3, 0, DRIFT_TERRAIN_TILEMAP_SIZE_LOG);
	float q = powf(0.5, roundf(mip_level))/256;
	bounds.l = (bounds.l + DRIFT_TERRAIN_MAP_SIZE/2)*q; bounds.r = (bounds.r + DRIFT_TERRAIN_MAP_SIZE/2)*q;
	bounds.b = (bounds.b + DRIFT_TERRAIN_MAP_SIZE/2)*q; bounds.t = (bounds.t + DRIFT_TERRAIN_MAP_SIZE/2)*q;
	
	int x0 = (int)floorf(bounds.l), x1 = (int)ceilf(bounds.r);
	int y0 = (int)floorf(bounds.b), y1 = (int)ceilf(bounds.t);
	uint level = (uint)roundf(mip_level);

	DRIFT_ARRAY(uint) tile_indexes = DRIFT_ARRAY_NEW(draw->mem, 256, uint);
	for(int y = y0; y < y1; y++){
		for(int x = x0; x < x1; x++){
			uint idx = tile_index(terra, (DriftTerrainTileCoord){x, y, level});
			if(idx) DRIFT_ARRAY_PUSH(tile_indexes, idx);
		}
	}
	
	return tile_indexes;
}

void DriftTerrainUpdateVisibility(DriftTerrain* terra, DriftVec2 pos){
	DriftRGBA8* visibility = terra->tilemap.visibility;
	DriftVec2 p = DriftAffinePoint(terra->world_to_map, pos);
	p.x -= 0.5f, p.y -= 0.5f;
	float radius = 1.5f;
	
	int x0 = (int)floorf(DriftClamp(p.x - radius, 0, DRIFT_TERRAIN_TILEMAP_SIZE)), x1 = (int)ceilf(DriftClamp(p.x + radius, 0, DRIFT_TERRAIN_TILEMAP_SIZE));
	int y0 = (int)floorf(DriftClamp(p.y - radius, 0, DRIFT_TERRAIN_TILEMAP_SIZE)), y1 = (int)ceilf(DriftClamp(p.y + radius, 0, DRIFT_TERRAIN_TILEMAP_SIZE));
	for(int y = y0; y < y1; y++){
		for(int x = x0; x < x1; x++){
			uint idx = x + y*DRIFT_TERRAIN_TILEMAP_SIZE;
			u8 value = (u8)(255*DriftSaturate(radius - hypotf(p.x - x, p.y - y)));
			if(value > visibility[idx].r) visibility[idx] = (DriftRGBA8){value, 0, 0, 0};
		}
	}
	
	terra->visibility_dirty = true;
}

// Gather rendering data.
void DriftTerrainDrawTiles(DriftDraw* draw, bool map_mode){
	TracyCZoneN(ZONE_TERRAIN, "Terrain", true);
	DriftTerrain* terra = draw->state->terra;
	terra->timestamp++;
	
	DriftMem* upload_mem = DriftZoneMemAquire(draw->shared->app->zone_heap, "UploadMem");
	UploadTilesContext* upload_ctx = DriftAlloc(upload_mem, sizeof(*upload_ctx));
	(*upload_ctx) = (UploadTilesContext){.draw_shared = draw->shared, .mem = upload_mem};
	
	DRIFT_ARRAY(uint) tile_indexes = DriftTerrainVisibleTiles(terra, draw);
	DRIFT_ARRAY_FOREACH(tile_indexes, idx_ptr){
		cache_tile(terra, *idx_ptr, upload_ctx);
		
		DriftTerrainTileCoord c = terra->tilemap.coord[*idx_ptr];
		DriftTerrainChunk chunk = {.x = c.x, .y = c.y, .level = c.level, .texture_idx = terra->tilemap.texture_idx[*idx_ptr]};
		DRIFT_ARRAY_PUSH(draw->terrain_chunks, chunk);
	}
	
	if(terra->biome_dirty){
		upload_ctx->biome_tex = terra->tilemap.biome;
		terra->biome_dirty = false;
	}
	
	if(terra->visibility_dirty && map_mode){
		upload_ctx->visibility_tex = terra->tilemap.visibility;
		terra->visibility_dirty = false;
	}
	
	tina_scheduler_enqueue(draw->shared->app->scheduler, upload_tiles, upload_ctx, 0, DRIFT_JOB_QUEUE_GFX, NULL);
	TracyCZoneEnd(ZONE_TERRAIN);
	
	// DriftAffine vp_inv = draw->vp_inverse;
	// float hw = fabsf(vp_inv.a) + fabsf(vp_inv.c), hh = fabsf(vp_inv.b) + fabsf(vp_inv.d);
	// DriftAABB2 bounds = {vp_inv.x - hw, vp_inv.y - hh, vp_inv.x + hw, vp_inv.y + hh};
	// gather_shadows(draw, terra, bounds);
}

void DriftTerrainDrawShadows(DriftDraw* draw, DriftTerrain* terra, DriftAABB2 bounds){
	float half_size = DRIFT_TERRAIN_MAP_SIZE/2;
	int x0 = (int)floorf((bounds.l + half_size)/256), x1 = (int)ceilf((bounds.r + half_size)/256);
	int y0 = (int)floorf((bounds.b + half_size)/256), y1 = (int)ceilf((bounds.t + half_size)/256);

	DRIFT_ARRAY(uint) tile_indexes = DRIFT_ARRAY_NEW(draw->mem, 256, uint);
	for(int y = y0; y < y1; y++){
		for(int x = x0; x < x1; x++){
			uint idx = tile_index(terra, (DriftTerrainTileCoord){x, y, 0});
			if(idx){
				if(DriftArrayLength(draw->shadow_masks) >= 16*1024){
					DRIFT_LOG("Ooops, too many shadows!");
					break;
				}
				
				DriftTerrainTileState state = terra->tilemap.state[idx];
				DRIFT_ASSERT(state != DRIFT_TERRAIN_TILE_STATE_DIRTY, "Unexpected tile state");
				if(state < DRIFT_TERRAIN_TILE_STATE_SHADOWS){
					u32 density[DRIFT_TERRAIN_TILE_SIZE_SQ];
					gather_tile_density(terra, idx, density);
					update_tile_shadows(terra, idx, density);
				}
				
				uint segment_count = terra->tilemap.collision[idx].count;
				DriftSegment* segments = DRIFT_ARRAY_RANGE(draw->shadow_masks, segment_count);
				memcpy(segments, terra->tilemap.collision[idx].segments, segment_count*sizeof(*segments));
				DriftArrayRangeCommit(draw->shadow_masks, segments + segment_count);
			}
		}
	}
	
	// DRIFT_LOG("shadow count %d", DriftArrayLength(draw->shadow_masks));
}

typedef struct {
	uint tile_idx;
	u8* sample;
	DriftVec2 grad;
} SampleInfo;

// Gather the index and sample location for a sample coordinate. 
// TODO derivative too?
static inline SampleInfo sample_info(DriftTerrain* terra, uint sx, uint sy){
	if(sx > DRIFT_TERRAIN_MAP_SIZE) sx = 0;
	if(sy > DRIFT_TERRAIN_MAP_SIZE) sy = 0;
	
	uint tile_idx = tile_index(terra, (DriftTerrainTileCoord){sx/DRIFT_TERRAIN_TILE_SIZE, sy/DRIFT_TERRAIN_TILE_SIZE});
	u8* samples = terra->tilemap.density[tile_idx].samples;
	
	uint tile_x = sx&(DRIFT_TERRAIN_TILE_SIZE - 1), tile_y = sy&(DRIFT_TERRAIN_TILE_SIZE - 1);
	uint idx0 = tile_x + tile_y*DRIFT_TERRAIN_TILE_SIZE;
	const uint mask_x = 1, mask_y = DRIFT_TERRAIN_TILE_SIZE;
	
	return (SampleInfo){
		.tile_idx = tile_idx,
		.sample = samples + idx0,
		.grad = {
			(0.5f/SDF_MAX_DIST)*(samples[idx0 | mask_x] - samples[idx0 & ~mask_x]),
			(0.5f/SDF_MAX_DIST)*(samples[idx0 | mask_y] - samples[idx0 & ~mask_y]),
		},
	};
}

static inline DriftVec3 dist_est(DriftVec3 cmin, float x0, float x1, float dx, float dy){
	float w = x0*x1 < 0 ? fabsf(x0 - x1)/(fabsf(x0) + FLT_MIN) : 0;
	return cmin.z > w ? cmin : (DriftVec3){{dx, dy, w}};
}

void DriftTerrainDig(DriftTerrain* terra, DriftVec2 pos, float radius){
	TracyCZoneN(ZONE_DIG, "Dig", true);
	
	pos.x -= DRIFT_TERRAIN_TILE_SCALE;
	pos.y -= DRIFT_TERRAIN_TILE_SCALE;
	radius = fminf(radius/DRIFT_TERRAIN_TILE_SCALE, SDF_MAX_DIST - 4);
	uint size = DRIFT_TERRAIN_TILE_SIZE;
	
	DriftVec2 map_coord = DriftAffinePoint(terra->world_to_map, pos);
	DriftVec2 sample_coord = DriftVec2Mul(map_coord, DRIFT_TERRAIN_TILE_SIZE);
	uint origin_x = (uint)sample_coord.x - DRIFT_TERRAIN_TILE_SIZE/2, origin_y = (uint)sample_coord.y - DRIFT_TERRAIN_TILE_SIZE/2;
	DriftVec2 dig_pos = {sample_coord.x - origin_x, sample_coord.y - origin_y};
	
	// Decode the SDF and dig into it.
	TracyCZoneN(ZONE_CARVE, "Carve", true);
	float values[DRIFT_TERRAIN_TILE_SIZE_SQ];
	for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
		for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++){
			// SDF of the digging.
			float dig = radius - hypotf(dig_pos.x - x, dig_pos.y - y);
			
			// Carve it by taking the minimum of the two distance fields.
			u8* sample = 	sample_info(terra, origin_x + x, origin_y + y).sample;
			values[x + y*DRIFT_TERRAIN_TILE_SIZE] = fmaxf(dig, DriftSDFDecode(*sample));
		}
	}
	TracyCZoneEnd(ZONE_CARVE);
	
	// Initialize the cells for the jump flooding to fix the negative areas.
	TracyCZoneN(ZONE_INIT, "Init", true);
	DriftVec3 cells0[DRIFT_TERRAIN_TILE_SIZE_SQ], cells1[DRIFT_TERRAIN_TILE_SIZE_SQ];
	for(int y = 0; y < (int)size; y++){
		float* values_row = values + y*size;
		DriftVec3* cells_row = cells0 + y*size;
		for(int x = 0; x < (int)size; x++){
			float dig = radius - hypotf(dig_pos.x - x, dig_pos.y - y);
			
			if(-values_row[x] > dig + 1){
				// Cell is fine, just need to take the absolute value.
				cells_row[x] = (DriftVec3){.z = fabsf(values_row[x])};
			} else {
				// Cell may have lost it's nearest point, initialize it.
				float v1 = values_row[x];
				DriftVec3 cmin = {};
				cmin = dist_est(cmin, v1, values_row[(x - 1)],  1, 0);
				cmin = dist_est(cmin, v1, values_row[(x + 1)], -1, 0);
				cmin = dist_est(cmin, v1, values_row[(x - (int)size)], 0,  1);
				cmin = dist_est(cmin, v1, values_row[(x + (int)size)], 0, -1);
				cmin = dist_est(cmin, v1, values_row[(x - 1 - (int)size)],  1,  1);
				cmin = dist_est(cmin, v1, values_row[(x + 1 - (int)size)], -1,  1);
				cmin = dist_est(cmin, v1, values_row[(x - 1 + (int)size)],  1, -1);
				cmin = dist_est(cmin, v1, values_row[(x + 1 + (int)size)], -1, -1);
				float w = cmin.z + FLT_MIN;
				cells_row[x] = (DriftVec3){{cmin.x/w, cmin.y/w, 0}};
			}
		}
	}
	TracyCZoneEnd(ZONE_INIT);
	
	TracyCZoneN(ZONE_FLOOD, "Flood", true);
	for(uint r = SDF_MAX_DIST/2; r > 0; r >>= 1){
		// Horizontal pass.
		for(uint i = 0; i < size; i += 1) DriftSDFFloodRow(cells1 + i, size, cells0 + i*size, size, r);
		for(uint i = 0; i < size; i += 1) DriftSDFFloodRow(cells0 + i, size, cells1 + i*size, size, r);
	}
	TracyCZoneEnd(ZONE_FLOOD);
	
	// Resolve the distance and re-encode it.
	TracyCZoneN(ZONE_RESOLVE, "Resolve", true);
	for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
		for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++){
			SampleInfo info = sample_info(terra, origin_x + x, origin_y + y);
			
			uint idx = x + y*DRIFT_TERRAIN_TILE_SIZE;
			float value = copysignf(DriftSDFValue(cells0[idx]), values[idx]);
			*info.sample = DriftSDFEncode(value);
			
			terra->tilemap.state[info.tile_idx] = DRIFT_TERRAIN_TILE_STATE_READY;
			// Mark parent tiles as dirty.
			DriftTerrainTileCoord c = terra->tilemap.coord[info.tile_idx];
			while(c.level < DRIFT_TERRAIN_TILEMAP_SIZE_LOG){
				c = (DriftTerrainTileCoord){c.x/2, c.y/2, c.level + 1};
				terra->tilemap.state[tile_index(terra, c)] = DRIFT_TERRAIN_TILE_STATE_DIRTY;
			}
		}
		
		// TODO mark tiles here instead of the inner loop.
	}
	TracyCZoneEnd(ZONE_RESOLVE);
	
	TracyCZoneEnd(ZONE_DIG);
}

uint DriftTerrainTileAt(DriftTerrain* terra, DriftVec2 pos){
	DriftVec2 map_coord = DriftAffinePoint(terra->world_to_map, pos);
	return tile_index(terra, (DriftTerrainTileCoord){(uint)map_coord.x, (uint)map_coord.y});
}

DriftTerrainSampleInfo DriftTerrainSampleCoarse(DriftTerrain* terra, DriftVec2 pos){
	DriftVec2 map_coord = DriftAffinePoint(terra->world_to_map, pos);
	DriftVec2 sample_coord = DriftVec2Mul(map_coord, DRIFT_TERRAIN_TILE_SIZE);
	
	SampleInfo info = sample_info(terra, (uint)(sample_coord.x - 0.5f), (uint)(sample_coord.y - 0.5f));
	return (DriftTerrainSampleInfo){.dist = DRIFT_TERRAIN_TILE_SCALE*DriftSDFDecode(*info.sample), .grad = DriftVec2Normalize(info.grad)};
}

DriftTerrainSampleInfo DriftTerrainSampleFine(DriftTerrain* terra, DriftVec2 pos){
	DriftVec2 map_coord = DriftAffinePoint(terra->world_to_map, pos);
	DriftVec2 sample_coord = DriftVec2Mul(map_coord, DRIFT_TERRAIN_TILE_SIZE);
	sample_coord.x -= 1; sample_coord.y -= 1;
	uint sx = (uint)sample_coord.x, sy = (uint)sample_coord.y;
	float dist00 = DriftSDFDecode(*sample_info(terra, sx + 0, sy + 0).sample);
	float dist10 = DriftSDFDecode(*sample_info(terra, sx + 1, sy + 0).sample);
	float dist01 = DriftSDFDecode(*sample_info(terra, sx + 0, sy + 1).sample);
	float dist11 = DriftSDFDecode(*sample_info(terra, sx + 1, sy + 1).sample);
	
	
	float fx = sample_coord.x - sx, fy = sample_coord.y - sy;
	float dist = DriftLerp(DriftLerp(dist00, dist10, fx), DriftLerp(dist01, dist11, fx), fy);
	
	DriftVec2 grad = {
		DriftLerp(dist10 - dist00, dist11 - dist01, fy),
		DriftLerp(dist01 - dist00, dist11 - dist10, fx),
	};
	
	return (DriftTerrainSampleInfo){.dist = DRIFT_TERRAIN_TILE_SCALE*dist, .grad = DriftVec2Normalize(grad)};
}

float DriftTerrainRaymarch(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold){
	DriftVec2 delta = DriftVec2Sub(b, a);
	float len = DriftVec2Length(delta);
	DriftVec2 dir = DriftVec2Mul(delta, 1/len);
	
	float t = 0;
	for(uint i = 0; i < 100 && t < 1; i++){
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(terra, DriftVec2FMA(a, delta, t));
		float adv = info.dist - radius;
		
		if(adv < threshold) return t;
		t += adv/len;
	}
	
	return 1;
}

float DriftTerrainRaymarch2(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold, float* min_dist){
	DriftVec2 delta = DriftVec2Sub(b, a);
	float len = DriftVec2Length(delta);
	DriftVec2 dir = DriftVec2Mul(delta, 1/len);
	*min_dist = INFINITY;
	
	float t = 0;
	for(uint i = 0; i < 100 && t < 1; i++){
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(terra, DriftVec2FMA(a, delta, t));
		float adv = info.dist - radius;
		*min_dist = fminf(*min_dist, adv);
		
		if(adv < threshold) return t;
		t += adv/len;
	}
	
	return 1;
}

DriftBiomeSample DriftTerrainSampleBiome(DriftTerrain* terra, DriftVec2 pos){
	DriftRGBA8* biome = terra->tilemap.biome;
	DriftVec2 p = DriftAffinePoint(terra->world_to_map, pos);
	p.x -= 0.5f, p.y -= 0.5f;
	
	uint ix = (uint)p.x, iy = (uint)p.y;
	if(ix > DRIFT_TERRAIN_TILEMAP_SIZE) ix = 0;
	if(iy > DRIFT_TERRAIN_TILEMAP_SIZE) iy = 0;
	uint idx = ix + DRIFT_TERRAIN_TILEMAP_SIZE*iy;
	DriftRGBA8 raw[] = {
		terra->tilemap.biome[idx + 0 + 0*DRIFT_TERRAIN_TILEMAP_SIZE],
		terra->tilemap.biome[idx + 1 + 0*DRIFT_TERRAIN_TILEMAP_SIZE],
		terra->tilemap.biome[idx + 0 + 1*DRIFT_TERRAIN_TILEMAP_SIZE],
		terra->tilemap.biome[idx + 1 + 1*DRIFT_TERRAIN_TILEMAP_SIZE],
	};
	
	DriftVec2 f = {p.x - ix, p.y - iy};
	DriftBiomeSample bio = {.weights = {
		DriftLerp(DriftLerp(raw[0].r, raw[1].r, f.x), DriftLerp(raw[2].r, raw[3].r, f.x), f.y)/255.0f,
		DriftLerp(DriftLerp(raw[0].g, raw[1].g, f.x), DriftLerp(raw[2].g, raw[3].g, f.x), f.y)/255.0f,
		DriftLerp(DriftLerp(raw[0].b, raw[1].b, f.x), DriftLerp(raw[2].b, raw[3].b, f.x), f.y)/255.0f,
		DriftLerp(DriftLerp(raw[0].a, raw[1].a, f.x), DriftLerp(raw[2].a, raw[3].a, f.x), f.y)/255.0f,
	}};
	bio.weights[4] = fmaxf(0, 1 - bio.weights[0] - bio.weights[1] - bio.weights[2] - bio.weights[3]);
	
	if(bio.weights[4] > 0.5){
		bio.idx = 4;
	} else {
		if(fmax(bio.weights[0], bio.weights[1]) > fmax(bio.weights[2], bio.weights[3])){
			bio.idx = bio.weights[0] > bio.weights[1] ? 0 : 1;
		} else {
			bio.idx = bio.weights[2] > bio.weights[3] ? 2 : 3;
		}
	}
	
	return bio;
}

// Terrain editing

static void clear_tiles(DriftTerrain* terra){
	printf("clearing tiles");
	
	DriftTerrainDensity* density = terra->tilemap.density + DRIFT_TERRAIN_MIP0;
	for(uint i = 0; i < 1024; i++){
		memset(density[i].samples, 0, sizeof(DriftTerrainDensity)/2);
	}
	
	DriftTerrainResetCache(terra);
}

float DriftTerrainEditAdd(float value, float dist, float radius, DriftVec2 pos, void* ctx){return fminf(value, dist - radius);}
float DriftTerrainEditSub(float value, float dist, float radius, DriftVec2 pos, void* ctx){return fmaxf(value, radius - dist);}

static float perlin_grad(int ix, int iy, float x, float y) {
	float random = 2920.f * sinf(ix * 21942.f + iy * 171324.f + 8912.f) * cosf(ix * 23157.f * iy * 217832.f + 9758.f);
	return (x - ix)*cosf(random) + (y - iy)*sinf(random);
}

static float perlin(float x, float y) {
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

float DriftTerrainEditPerlin(float value, float dist, float radius, DriftVec2 pos, void* ctx){
	DriftTerrainEditPerlinParams* params = ctx;
	
	if(dist < radius){
		float value = 0, freq = params->frq/32, denom = 1;
		for(uint oct = 0; oct < params->oct; oct++){
			value += perlin(pos.x*freq, pos.y*freq)*denom;
			freq *= 2, denom *= params->exp;
		}
		return params->add - params->mul*fabsf(value);
	} else {
		return value;
	}
}

void DriftTerrainEdit(DriftTerrain* terra, DriftVec2 pos, float radius, DriftTerrainEditFunc func, void* ctx){
	pos.x -= DRIFT_TERRAIN_TILE_SCALE;
	pos.y -= DRIFT_TERRAIN_TILE_SCALE;
	
	DriftVec2 m = DriftAffinePoint(terra->world_to_map, pos);
	DriftVec2 s = DriftVec2Mul(m, DRIFT_TERRAIN_TILE_SIZE);
	float r = radius/DRIFT_TERRAIN_TILE_SCALE;
	
	float hs = r + SDF_MAX_DIST;
	float max = DRIFT_TERRAIN_TILEMAP_SIZE*DRIFT_TERRAIN_TILE_SIZE;
	float x0 = DriftClamp(floorf(s.x - hs), 0, max), x1 = DriftClamp(ceilf(s.x + hs), 0, max);
	float y0 = DriftClamp(floorf(s.y - hs), 0, max), y1 = DriftClamp(ceilf(s.y + hs), 0, max);
	
	for(float y = y0; y < y1; y++){
		for(float x = x0; x < x1; x++){
			DriftVec2 pos = {x, y};
			SampleInfo info = sample_info(terra, (uint)x, (uint)y);
			float value = DriftSDFDecode(*info.sample);
			float dist = DriftVec2Distance(s, pos);
			
			*info.sample = DriftSDFEncode(func(value, dist, r, pos, ctx));
			terra->tilemap.state[info.tile_idx] = DRIFT_TERRAIN_TILE_STATE_READY;
			DriftTerrainTileCoord c = terra->tilemap.coord[info.tile_idx];
			while(c.level < DRIFT_TERRAIN_TILEMAP_SIZE_LOG){
				c = (DriftTerrainTileCoord){c.x/2, c.y/2, c.level + 1};
				terra->tilemap.state[tile_index(terra, c)] = DRIFT_TERRAIN_TILE_STATE_DIRTY;
			}
		}
	}
}

void DriftBiomeEdit(DriftTerrain* terra, DriftVec2 pos, float radius, DriftRGBA8 _value){
	DriftRGBA8* biome = terra->tilemap.biome;
	DriftVec2 p = DriftAffinePoint(terra->world_to_map, pos);
	p.x -= 0.5f, p.y -= 0.5f;
	radius /= DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILE_SCALE;
	
	float value[5] = {_value.r, _value.g, _value.b, _value.a};
	value[4] = 255 - value[0] - value[1] - value[2] - value[3];
	
	for(uint y = 0; y < DRIFT_TERRAIN_TILEMAP_SIZE; y++){
		for(uint x = 0; x < DRIFT_TERRAIN_TILEMAP_SIZE; x++){
			uint idx = x + y*DRIFT_TERRAIN_TILEMAP_SIZE;
			DriftRGBA8 _sample = biome[idx];
			float sample[5] = {_sample.r, _sample.g, _sample.b, _sample.a};
			sample[4] = 255 - sample[0] - sample[1] - sample[2] - sample[3];
			float dot = sample[0]*value[0] + sample[1]*value[1] + sample[2]*value[2] + sample[3]*value[3] + sample[4]*value[4];
			float alpha = fmaxf(0, DriftSaturate(radius - hypotf(p.x - x, p.y - y) - dot/(255*255)));
			
			// Round up when interpolating the values.
			sample[0] = ceilf(DriftLerp(sample[0], value[0], alpha));
			sample[1] = ceilf(DriftLerp(sample[1], value[1], alpha));
			sample[2] = ceilf(DriftLerp(sample[2], value[2], alpha));
			sample[3] = ceilf(DriftLerp(sample[3], value[3], alpha));
			sample[4] = ceilf(DriftLerp(sample[4], value[4], alpha));
			
			// Renormalize
			float coef = fminf(1, 2 - (sample[0] + sample[1] + sample[2] + sample[3])/255);
			biome[idx] = (DriftRGBA8){(u8)(coef*sample[0]), (u8)(coef*sample[1]), (u8)(coef*sample[2]), (u8)(coef*sample[3])};
		}
	}
	
	terra->biome_dirty = true;
}

void DriftTerrainEditIO(tina_job* job, DriftTerrain* terra, bool save){
	DriftTerrainDensity* density = terra->tilemap.density + DRIFT_TERRAIN_MIP0;
	size_t density_size = DRIFT_TERRAIN_TILEMAP_SIZE_SQ*sizeof(DriftTerrainDensity);
	
	{
		const char* filename = "../bin/biome.bin";
		FILE* file = fopen(filename, save ? "wb" : "rb");
		DRIFT_ASSERT_HARD(file, "Failed to open '%s' for writting.", filename);
		
		size_t size = sizeof(DriftRGBA8), len = DRIFT_TERRAIN_TILEMAP_SIZE_SQ;
		if(save) fwrite(terra->tilemap.biome, size, len, file); else fread(terra->tilemap.biome, size, len, file);
		fclose(file);
	}
	
	{
		const char* filename = "../bin/terrain.raw";
		FILE* file = fopen(filename, save ? "wb" : "rb");
		DRIFT_ASSERT_HARD(file, "Failed to open '%s' for writting.", filename);
		
		if(save) fwrite(density, density_size, 1, file); else fread(density, density_size, 1, file);
		fclose(file);
	}
	
	DriftTerrainResetCache(terra);
	DRIFT_LOG(save ? "Saved" : "Loaded");
}

#define ROWS_PER_JOB 64
#define MAP_SIZE (DRIFT_TERRAIN_TILEMAP_SIZE*DRIFT_TERRAIN_TILE_SIZE)

typedef struct {
	DriftTerrain* terra;
	float* values;
	DriftVec3* cells[2];
} RectifyContext;

static void init_job(tina_job* job){
	RectifyContext* ctx = tina_job_get_description(job)->user_data;
	uint j = tina_job_get_description(job)->user_idx;
	if(j % 256 == 0){printf("\r  init: %d%%", 100*j/MAP_SIZE); fflush(stdout);}
	
	float* x = ctx->values + j*MAP_SIZE;
	DriftVec3* c = ctx->cells[0] + j*MAP_SIZE;
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
	float* progress;
	float p0, p1;
	
	DriftVec3* dst;
	DriftVec3* src;
	int r;
} FloodContext;

static void flood_job(tina_job* job){
	FloodContext* ctx = tina_job_get_description(job)->user_data;
	uint i0 = tina_job_get_description(job)->user_idx*ROWS_PER_JOB;
	for(uint i = i0; i < i0 + ROWS_PER_JOB; i += 1) DriftSDFFloodRow(ctx->dst + i, MAP_SIZE, ctx->src + i*MAP_SIZE, MAP_SIZE, ctx->r);
	
	*ctx->progress = DriftLerp(ctx->p0, ctx->p1, (float)i0/(float)MAP_SIZE);
}

static void rectify(tina_job* job){
	DriftTerrain* terra = tina_job_get_description(job)->user_data;
	
	RectifyContext* ctx = DRIFT_COPY(DriftSystemMem, ((RectifyContext){
		.terra = terra,
		.values = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(float)),
		.cells[0] = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(DriftVec3)),
		.cells[1] = DriftAlloc(DriftSystemMem, MAP_SIZE*MAP_SIZE*sizeof(DriftVec3)),
	}));
	
	float progress_cursor = terra->rectify_progress = 0;
	
	size_t tile_stride = DRIFT_TERRAIN_TILE_SIZE;
	size_t map_stride = DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILEMAP_SIZE;
	for(uint i = 0; i < DRIFT_TERRAIN_TILEMAP_SIZE_SQ; i++){
		uint x = i % DRIFT_TERRAIN_TILEMAP_SIZE, y = i / DRIFT_TERRAIN_TILEMAP_SIZE;
		float* dst_origin = ctx->values + x*tile_stride + y*DRIFT_TERRAIN_TILE_SIZE_SQ*DRIFT_TERRAIN_TILEMAP_SIZE;
		u8* samples = ctx->terra->tilemap.density[i + DRIFT_TERRAIN_MIP0].samples;
		for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
			float* dst = dst_origin + y*map_stride;
			u8* src = samples + y*tile_stride;
			for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++) dst[x] = DriftSDFDecode(src[x]);
		}
	}
	
	ctx->terra->rectify_progress = 0.02f;
	progress_cursor = 0.05f;
	DriftThrottledParallelFor(job, init_job, ctx, MAP_SIZE);
	
	for(uint r = SDF_MAX_DIST/2; r > 0; r >>= 1){
		FloodContext fctx = {.r = r, .progress = &ctx->terra->rectify_progress};
		float inc = 0.15f;
		
		fctx.p0 = progress_cursor, progress_cursor += inc, fctx.p1 = progress_cursor;
		fctx.dst = ctx->cells[1], fctx.src = ctx->cells[0];
		DriftThrottledParallelFor(job, flood_job, &fctx, MAP_SIZE/ROWS_PER_JOB);
		
		fctx.p0 = progress_cursor, progress_cursor += inc, fctx.p1 = progress_cursor;
		fctx.dst = ctx->cells[0], fctx.src = ctx->cells[1];
		DriftThrottledParallelFor(job, flood_job, &fctx, MAP_SIZE/ROWS_PER_JOB);
	}
	
	ctx->terra->rectify_progress = progress_cursor;
	for(uint i = 0; i < MAP_SIZE*MAP_SIZE; i++) ctx->values[i] = copysignf(DriftSDFValue(ctx->cells[0][i]), ctx->values[i]);
	
	DriftDealloc(DriftSystemMem, ctx->cells[0], 0);
	DriftDealloc(DriftSystemMem, ctx->cells[1], 0);
	
	for(uint i = 0; i < DRIFT_TERRAIN_TILEMAP_SIZE_SQ; i++){
		uint x = i % DRIFT_TERRAIN_TILEMAP_SIZE, y = i / DRIFT_TERRAIN_TILEMAP_SIZE;
		u8* dst_tile = ctx->terra->tilemap.density[i + DRIFT_TERRAIN_MIP0].samples;
		float* samples = ctx->values + x*tile_stride + y*DRIFT_TERRAIN_TILE_SIZE_SQ*DRIFT_TERRAIN_TILEMAP_SIZE;
		for(uint y = 0; y < DRIFT_TERRAIN_TILE_SIZE; y++){
			u8* dst = dst_tile + y*tile_stride;
			float* src = samples + y*map_stride;
			for(uint x = 0; x < DRIFT_TERRAIN_TILE_SIZE; x++) dst[x] = DriftSDFEncode(src[x]);
		}
	}
	ctx->terra->rectify_progress = 1;
	
	DriftTerrainResetCache(ctx->terra);
	DriftDealloc(DriftSystemMem, ctx->values, 0);
	DriftDealloc(DriftSystemMem, ctx, sizeof(*ctx));
}

void DriftTerrainEditRectify(DriftTerrain* terra, tina_scheduler* sched, tina_group* group){
	tina_scheduler_enqueue(sched, rectify, terra, 0, DRIFT_JOB_QUEUE_WORK, group);
}

void DriftTerrainEditEnter(){
	
}

void DriftTerrainEditExit(tina_job* job){
	rectify(job);
	
	DriftTerrain* terra = tina_job_get_description(job)->user_data;
	DriftTerrainDensity* density = terra->tilemap.density + DRIFT_TERRAIN_MIP0;
	size_t density_size = DRIFT_TERRAIN_TILEMAP_SIZE_SQ*sizeof(DriftTerrainDensity);
	
	DriftThrottledParallelFor(job, encode_tile, density, DRIFT_TERRAIN_TILEMAP_SIZE_SQ);
	size_t len = density_size/DRIFT_TERRAIN_FILE_CHUNKS;
	for(uint i = 0; i < DRIFT_TERRAIN_FILE_CHUNKS; i++){
		char filename[64];
		snprintf(filename, sizeof(filename), "../bin/terrain%d.bin", i);
		
		FILE* file = fopen(filename, "wb");
		DRIFT_ASSERT_HARD(file, "Failed to open '%s' for writting.", filename);
		fwrite((u8*)density + i*len, 1, len, file);
		fclose(file);
	}
	
	DriftThrottledParallelFor(job, decode_tiles, density, DRIFT_TERRAIN_FILE_CHUNKS*CHUNK_SPLITS);
	DriftTerrainResetCache(terra);
	
	DRIFT_LOG("Wrote bin/terrain*.bin");
}

/* TODO
 * Undo
*/
