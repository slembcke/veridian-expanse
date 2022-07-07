#define DRIFT_ATLAS_SIZE 256

#define DRIFT_LIGHTFIELD_SCALE 8
#define DRIFT_SHADOWFIELD_SCALE 4

typedef struct {
	DriftApp* app;
	const DriftGfxDriver* driver;
	
	// Resources.
	DriftVec2 color_buffer_size;
	DriftGfxTexture* color_buffer[2];
	DriftGfxRenderTarget* color_target[2];
	uint color_buffer_index;
	
	DriftGfxTexture* lightfield_buffer;
	DriftGfxRenderTarget* lightfield_target[2];
	
	DriftGfxTexture* shadowfield_buffer;
	DriftGfxRenderTarget* shadowfield_target[2];
	
	DriftGfxSampler* nearest_sampler;
	DriftGfxSampler* linear_sampler;
	DriftGfxSampler* terrain_sampler;
	
	DriftGfxPipeline* present_pipeline;
	DriftGfxPipeline* primitive_pipeline;
	DriftGfxPipeline* light_pipeline[2];
	DriftGfxPipeline* light_blit_pipeline[2];
	DriftGfxPipeline* shadow_mask_pipeline[2];
	DriftGfxPipeline* shadow_pipeline[2];
	DriftGfxPipeline* terrain_pipeline;
	DriftGfxPipeline* sprite_pipeline;
	DriftGfxPipeline* overlay_sprite_pipeline;
	
	DriftGfxPipeline* debug_lightfield_pipeline;
	DriftGfxPipeline* debug_terrain_pipeline;
	DriftGfxPipeline* debug_delta_pipeline;
	
	DriftGfxTexture* atlas_texture;
	DriftGfxTexture* terrain_tiles;
} DriftDrawShared;

DriftDrawShared* DriftDrawSharedNew(DriftApp* app, tina_job* job);
void DriftDrawSharedFree(DriftDrawShared* draw_shared);

typedef struct {
	DriftVec2 a, b;
} DriftSegment;

typedef struct {
	DriftVec2 p0, p1;
	float radii[2];
	DriftRGBA8 color;
} DriftPrimitive;

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;
typedef struct DriftInputIcon DriftInputIcon;

typedef struct {
	float x, y, level, texture_idx;
} DriftTerrainChunk;

typedef struct {
	DriftGameContext* ctx;
	DriftGameState* state;
	DriftDrawShared* shared;
	tina_job* job;
	tina_group jobs;
	
	DriftZoneMem* zone;
	DriftMem* mem;
	
	float dt, dt_since_tick; // Elapsed time since last tick.
	DriftVec2 pixel_extent, screen_extent, buffer_extent;
	DriftAffine v_matrix, p_matrix, vp_matrix, vp_inverse, reproj_matrix;
	
	DRIFT_ARRAY(DriftTerrainChunk) terrain_chunks;
	DRIFT_ARRAY(DriftLight) lights;
	DRIFT_ARRAY(DriftSegment) shadow_masks;
	DRIFT_ARRAY(DriftSprite) bg_sprites;
	DRIFT_ARRAY(DriftPrimitive) bg_prims;
	DRIFT_ARRAY(DriftSprite) fg_sprites;
	DRIFT_ARRAY(DriftSprite) overlay_sprites;
	DRIFT_ARRAY(DriftPrimitive) overlay_prims;
	DRIFT_ARRAY(DriftSprite) hud_sprites;
	
	DriftGfxRenderer* renderer;
	DriftGfxBufferBinding globals_binding;
	DriftGfxBufferBinding quad_vertex_binding;
	DriftGfxBufferBinding quad_index_binding;
	DriftGfxTexture* color_buffer_prev;
	
	const DriftInputIcon* input_icons;
} DriftDraw;

DriftDraw* DriftDrawCreate(DriftGameContext* ctx, tina_job* job);

DriftVec2 DriftDrawScreenExtent(DriftVec2 pixel_extent);
DriftVec2 DriftDrawBufferExtent(DriftVec2 screen_extent);
void DriftDrawResizeBuffers(DriftDraw* draw, DriftVec2 buffer_extent);

DriftGfxPipelineBindings* DriftDrawQuads(DriftDraw* draw, DriftGfxPipeline* pipeline, u32 count);

#define DRIFT_TEXT_BLACK "{#00000000}"
#define DRIFT_TEXT_GRAY  "{#80808080}"
#define DRIFT_TEXT_WHITE "{#FFFFFFFF}"
#define DRIFT_TEXT_RED   "{#FF0000FF}"
#define DRIFT_TEXT_GREEN "{#00FF00FF}"
#define DRIFT_TEXT_BLUE  "{#0000FFFF}"
DriftAffine DriftDrawText(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftAffine matrix, const char* string);
DriftAffine DriftDrawTextF(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftAffine matrix, const char* format, ...);
