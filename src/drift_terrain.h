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

#define DRIFT_TERRAIN_FILE_SPLITS 8

typedef struct {
	uint x, y, level;
} DriftTerrainTileCoord;

typedef enum {
	DRIFT_TERRAIN_TILE_STATE_DIRTY,
	DRIFT_TERRAIN_TILE_STATE_READY,
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
		DriftRGBA8 biome[DRIFT_TERRAIN_TILE_COUNT];
		DriftTerrainTileCoord coord[DRIFT_TERRAIN_TILE_COUNT];
		DriftTerrainTileState state[DRIFT_TERRAIN_TILE_COUNT];
		DriftTerrainDensity density[DRIFT_TERRAIN_TILE_COUNT];
		u16 texture_idx[DRIFT_TERRAIN_TILE_COUNT];
		u64 timestamps[DRIFT_TERRAIN_TILE_COUNT];
		
		struct {
			DriftSegment* segments;
			uint count;
		} collision[DRIFT_TERRAIN_TILE_COUNT];
	} tilemap;
	
	bool biome_dirty;
	
	DriftTerrainCacheEntry cache_heap[DRIFT_TERRAIN_TILECACHE_SIZE];
	u64 timestamp;
	
	tina_group jobs;
} DriftTerrain;

DriftTerrain* DriftTerrainNew(tina_job* job, bool force_regen);
void DriftTerrainFree(DriftTerrain* terra);

void DriftTerrainResetCache(DriftTerrain* terra);

void DriftTerrainDraw(DriftDraw* draw);

void DriftTerrainDig(DriftTerrain* terra, DriftVec2 pos, float radius);

uint DriftTerrainTileAt(DriftTerrain* terra, DriftVec2 pos);

typedef struct {
	float dist;
	DriftVec2 grad;
} DriftTerrainSampleInfo;

DriftTerrainSampleInfo DriftTerrainSampleCoarse(DriftTerrain* terra, DriftVec2 pos);
DriftTerrainSampleInfo DriftTerrainSampleFine(DriftTerrain* terra, DriftVec2 pos);
float DriftTerrainRaymarch(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold);
float DriftTerrainRaymarch2(DriftTerrain* terra, DriftVec2 a, DriftVec2 b, float radius, float threshold, float* min_dist);

uint DriftTerrainSampleBiome(DriftTerrain* terra, DriftVec2 pos);

typedef struct {
	float frq, oct, exp, mul, add;
} DriftTerrainEditPerlinParams;

typedef float DriftTerrainEditFunc(float value, float dist, float radius, DriftVec2 pos, void* ctx);
DriftTerrainEditFunc DriftTerrainEditAdd, DriftTerrainEditSub, DriftTerrainEditPerlin;
void DriftTerrainEdit(DriftTerrain* terra, DriftVec2 pos, float radius, DriftTerrainEditFunc func, void* ctx);

void DriftBiomeEdit(DriftTerrain* terra, DriftVec2 pos, float radius, DriftRGBA8 value);

void DriftTerrainEditIO(tina_job* job, DriftTerrain* terra, bool save);
void DriftTerrainEditRectify(DriftTerrain* terra, tina_job* job);
