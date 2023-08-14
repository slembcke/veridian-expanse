/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#define DRIFT_TERRAIN_TILE_SCALE 8
#define DRIFT_TERRAIN_TILE_SIZE 32
#define DRIFT_TERRAIN_TILE_SIZE_SQ (DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILE_SIZE)

#define DRIFT_TERRAIN_TILEMAP_SIZE_LOG 8
#define DRIFT_TERRAIN_TILEMAP_SIZE (1 << DRIFT_TERRAIN_TILEMAP_SIZE_LOG)
#define DRIFT_TERRAIN_TILEMAP_SIZE_SQ (DRIFT_TERRAIN_TILEMAP_SIZE*DRIFT_TERRAIN_TILEMAP_SIZE)

#define DRIFT_TERRAIN_MAP_SIZE (DRIFT_TERRAIN_TILEMAP_SIZE*DRIFT_TERRAIN_TILE_SIZE*DRIFT_TERRAIN_TILE_SCALE)
#define DRIFT_TERRAIN_TILE_COUNT ((4<<(2*DRIFT_TERRAIN_TILEMAP_SIZE_LOG))/3)
#define DRIFT_TERRAIN_MIP0 (DRIFT_TERRAIN_TILE_COUNT/4)

#define DRIFT_TERRAIN_TILECACHE_SIZE 1024

#define DRIFT_TERRAIN_FILE_CHUNKS 8

void DriftTerrainLoadBase(tina_scheduler* sched);

typedef struct {
	uint x, y, level;
} DriftTerrainTileCoord;

// TODO should shadows be a separate state?
// Otherwise caching a texture implies shadows are ready.

typedef enum {
	// Tile density needs to be refreshed.
	DRIFT_TERRAIN_TILE_STATE_DIRTY,
	// Tile density is ready to be used.
	DRIFT_TERRAIN_TILE_STATE_READY,
	// Tile density and shadows are ready to be used.
	DRIFT_TERRAIN_TILE_STATE_SHADOWS,
	// Tile density, shadows, and texture are ready to be used.
	DRIFT_TERRAIN_TILE_STATE_CACHED,
} DriftTerrainTileState;

typedef struct {
	u8 samples[DRIFT_TERRAIN_TILE_SIZE_SQ];
} DriftTerrainDensity;

typedef struct {
	u64 timestamp;
	u32 tile_idx;
	u32 texture_idx;
} DriftTerrainCacheEntry;

typedef struct {
	DriftAffine map_to_world;
	DriftAffine world_to_map;
	
	struct {
		DriftRGBA8 biome[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
		DriftRGBA8 visibility[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
		u8 resources[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
		u8 biomass[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
		
		DriftTerrainTileCoord coord[DRIFT_TERRAIN_TILE_COUNT];
		DriftTerrainTileState state[DRIFT_TERRAIN_TILE_COUNT];
		DriftTerrainDensity density[DRIFT_TERRAIN_TILE_COUNT];
		u16 texture_idx[DRIFT_TERRAIN_TILE_COUNT];
		u64 timestamps[DRIFT_TERRAIN_TILE_COUNT];
		
		// TODO overallocated?
		struct {
			DriftSegment* segments;
			uint count;
		} collision[DRIFT_TERRAIN_TILE_COUNT];
	} tilemap;
	
	bool biome_dirty, visibility_dirty;
	float rectify_progress;
	
	DriftTerrainCacheEntry cache_heap[DRIFT_TERRAIN_TILECACHE_SIZE];
	u64 timestamp;
	
	tina_group jobs;
} DriftTerrain;

DriftTerrain* DriftTerrainNew(tina_job* job, bool force_regen);
void DriftTerrainFree(DriftTerrain* terra);

void DriftTerrainResetCache(DriftTerrain* terra);
void DriftTerrainUpdateVisibility(DriftTerrain* terra, DriftVec2 pos);
void DriftTerrainDrawTiles(DriftDraw* draw, bool map_mode);
void DriftTerrainGatherShadows(DriftDraw* draw, DriftTerrain* terra, DriftAABB2 bounds);

void DriftTerrainDig(DriftTerrain* terra, DriftVec2 pos, float radius);

uint DriftTerrainTileAt(DriftTerrain* terra, DriftVec2 pos);

#define DRIFT_TERRAIN_TILE_RADIUS (DRIFT_TERRAIN_TILE_SCALE*DRIFT_TERRAIN_TILE_SIZE/2)
uint DriftTerrainSpawnTileIndexes(DriftTerrain* terra, uint indexes[], uint count, DriftVec2 center, float spawn_radius);
uint DriftTerrainSpawnLocations(DriftTerrain* terra, DriftVec2 locations[], uint count, uint tile_idx, uint seed, float radius);
uint DriftTerrainTileResources(DriftTerrain* terra, uint tile_idx);
void DriftTerrainTileResourcesInc(DriftTerrain* terra, uint tile_idx);
uint DriftTerrainTileBiomass(DriftTerrain* terra, uint tile_idx);
void DriftTerrainTileBiomassInc(DriftTerrain* terra, uint tile_idx);

typedef struct {
	float dist;
	DriftVec2 grad;
} DriftTerrainSampleInfo;

DriftTerrainSampleInfo DriftTerrainSampleCoarse(DriftTerrain* terra, DriftVec2 pos);
DriftTerrainSampleInfo DriftTerrainSampleFine(DriftTerrain* terra, DriftVec2 pos);
float DriftTerrainRaymarch(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold);
float DriftTerrainRaymarch2(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold, float* min_dist);

typedef struct {
	uint idx;
	float weights[5];
} DriftBiomeSample;

DriftBiomeSample DriftTerrainSampleBiome(DriftTerrain* terra, DriftVec2 pos);

void DriftTerrainEditExit(tina_job* job);

typedef struct {
	float frq, oct, exp, mul, add;
} DriftTerrainEditPerlinParams;

typedef float DriftTerrainEditFunc(float value, float dist, float radius, DriftVec2 pos, void* ctx);
DriftTerrainEditFunc DriftTerrainEditAdd, DriftTerrainEditSub, DriftTerrainEditPerlin, DriftTerrainEditPerlinAdd, DriftTerrainEditPerlinSub;
void DriftTerrainEdit(DriftUpdate* update, DriftVec2 pos, float radius, DriftTerrainEditFunc* func, void* ctx);

void DriftBiomeEdit(DriftTerrain* terra, DriftVec2 pos, float radius, DriftRGBA8 value);
void DriftBiomeSpaceEdit(DriftTerrain* terra, DriftVec2 pos, float radius, float value);

void DriftTerrainEditIO(tina_job* job, DriftTerrain* terra, bool save);
void DriftTerrainEditRectify(DriftTerrain* terra, tina_scheduler* sched, tina_group* group);
