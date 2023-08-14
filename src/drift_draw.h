/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#define DRIFT_ATLAS_SIZE 256

typedef struct {
	const DriftGfxDriver* driver;
	
	// Resources.
	DriftVec2 color_buffer_size;
	float lightfield_scale, hires;
	
	DriftGfxTexture* lightfield_buffer;
	DriftGfxTexture* shadowfield_buffer;
	DriftGfxTexture* color_buffer[2];
	DriftGfxTexture* resolve_buffer;
	
	DriftGfxRenderTarget* lightfield_target[2];
	DriftGfxRenderTarget* shadowfield_target[2];
	DriftGfxRenderTarget* color_target[2];
	DriftGfxRenderTarget* resolve_target;
	
	DriftGfxSampler* nearest_sampler;
	DriftGfxSampler* linear_sampler;
	DriftGfxSampler* repeat_sampler;
	
	DriftGfxPipeline* overlay_primitive_pipeline;
	DriftGfxPipeline* linear_primitive_pipeline;
	DriftGfxPipeline* plasma_pipeline;
	DriftGfxPipeline* light_pipeline[2];
	DriftGfxPipeline* light_blit_pipeline[2];
	DriftGfxPipeline* shadow_mask_pipeline[2];
	DriftGfxPipeline* shadow_pipeline[2];
	DriftGfxPipeline* terrain_pipeline;
	DriftGfxPipeline* terrain_map_pipeline;
	DriftGfxPipeline* sprite_pipeline;
	DriftGfxPipeline* flash_sprite_pipeline;
	DriftGfxPipeline* overlay_sprite_pipeline;
	DriftGfxPipeline* haze_pipeline;
	DriftGfxPipeline* resolve_pipeline;
	DriftGfxPipeline* image_blit_pipeline;
	DriftGfxPipeline* pause_blit_pipeline;
	DriftGfxPipeline* map_blit_pipeline;
	DriftGfxPipeline* present_pipeline;
	
	DriftGfxPipeline* debug_lightfield_pipeline;
	DriftGfxPipeline* debug_terrain_pipeline;
	DriftGfxPipeline* debug_delta_pipeline;
	
	DriftGfxTexture* atlas_texture;
	DriftGfxTexture* terrain_tiles;
} DriftDrawShared;

DriftDrawShared* DriftDrawSharedNew(tina_job* job, float lightfield_scale);
void DriftDrawSharedFree(DriftDrawShared* draw_shared);

typedef struct {
	DriftVec2 a, b;
} DriftSegment;

typedef struct {
	DriftVec2 p0, p1;
	float radii[2];
	DriftRGBA8 color;
} DriftPrimitive;

typedef struct {
	DriftVec2 pos;
	float value;
} DriftPlasmaVert;

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;
typedef struct DriftInputIcon DriftInputIcon;

typedef struct {
	float x, y, level, texture_idx;
} DriftTerrainChunk;

typedef struct {
	DriftGPUMatrix v_matrix, p_matrix, terrain_matrix;
	DriftGPUMatrix vp_matrix, vp_inverse, reproj_matrix;
	DriftVec2 jitter;
	DriftVec2 raw_extent, virtual_extent, internal_extent;
	float atlas_size, sharpening, gradmul;
	float biome_layer, visibility_layer;
} DriftGlobalUniforms;

struct DriftDraw {
	tina_job* job;
	tina_group jobs;
	DriftMem* mem;
	
	DriftDrawShared* shared;
	DriftGameContext* ctx;
	DriftGameState* state;
	
	// Current nanoseconds since launch.
	u64 nanos;
	// Current frame and tick count.
	uint frame, tick;
	// Elapsed time since last frame and tick.
	float dt, dt_since_tick;
	// Size of the window/framebuffer in pixels.
	DriftVec2 raw_extent;
	// The raw extent, but scaled to the effective virtual resolution.
	DriftVec2 virtual_extent;
	// The rounded up size of the internal rendering buffer.
	DriftVec2 internal_extent;
	DriftAffine v_matrix, p_matrix, vp_matrix, vp_inverse, ui_matrix, reproj_matrix;
	
	DriftVec4 screen_tint;
	DRIFT_ARRAY(DriftTerrainChunk) terrain_chunks;
	DRIFT_ARRAY(DriftLight) lights;
	DRIFT_ARRAY(DriftSegment) shadow_masks;
	DRIFT_ARRAY(DriftSprite) bg_sprites;
	DRIFT_ARRAY(DriftPrimitive) bg_prims;
	DRIFT_ARRAY(DriftSegment) plasma_strands;
	DRIFT_ARRAY(DriftSprite) fg_sprites;
	DRIFT_ARRAY(DriftSprite) flash_sprites;
	DRIFT_ARRAY(DriftSprite) bullet_sprites;
	DRIFT_ARRAY(DriftSprite) overlay_sprites;
	DRIFT_ARRAY(DriftPrimitive) overlay_prims;
	DRIFT_ARRAY(DriftSprite) hud_sprites;
	
	DriftGfxRenderer* renderer;
	DriftGlobalUniforms global_uniforms;
	DriftGfxBufferBinding globals_binding;
	DriftGfxBufferBinding ui_binding;
	DriftGfxBufferBinding quad_vertex_binding;
	DriftGfxBufferBinding quad_index_binding;
	DriftGfxPipelineBindings default_bindings;
	DriftGfxRenderTarget* color_target_curr;
	DriftGfxRenderTarget* color_target_prev;
	DriftGfxTexture* color_buffer_curr;
	DriftGfxTexture* color_buffer_prev;
	bool prev_buffer_invalid;
};

DriftDraw* DriftDrawBeginBase(tina_job* job, DriftGameContext* ctx, DriftAffine v_matrix, DriftAffine prev_vp_matrix);
DriftDraw* DriftDrawBegin(DriftUpdate* update, float dt_since_tick, DriftAffine v_matrix, DriftAffine prev_vp_matrix);
void DriftDrawBindGlobals(DriftDraw* draw);

DriftGfxPipelineBindings* DriftDrawQuads(DriftDraw* draw, DriftGfxPipeline* pipeline, u32 count);

#define DRIFT_TEXT_BLACK  "{#00000000}"
#define DRIFT_TEXT_GRAY   "{#80808080}"
#define DRIFT_TEXT_WHITE  "{#FFFFFFFF}"
#define DRIFT_TEXT_RED    "{#FF0000FF}"
#define DRIFT_TEXT_GREEN  "{#00FF00FF}"
#define DRIFT_TEXT_BLUE   "{#0000FFFF}"
#define DRIFT_TEXT_YELLOW "{#FFFF00FF}"
#define DRIFT_TEXT_ORANGE "{#FF8000FF}"

DriftAABB2 DriftDrawTextBounds(const char* string, size_t n);
static inline DriftVec2 DriftDrawTextSize(const char* string, size_t n){
	DriftAABB2 bb = DriftDrawTextBounds(string, n);
	return (DriftVec2){bb.r - bb.l, bb.t - bb.b};
}

typedef struct {
	DriftVec4 tint;
	DriftAffine matrix;
	uint glyph_limit;
	float max_width;
	int spacing_adjust;
	bool* active;
	bool* pause;
} DriftTextOptions;

DriftAffine DriftDrawTextFull(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, const char* string, DriftTextOptions opts);
DriftVec2 DriftDrawText(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftVec2 origin, const char* string);
DriftVec2 DriftDrawTextF(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftVec2 origin, const char* format, ...);

typedef struct {
	DRIFT_ARRAY(void) arr;
	DriftGfxPipeline* pipeline;
	DriftGfxPipelineBindings* bindings;
} DriftDrawBatch;

void DriftDrawBatches(DriftDraw* draw, DriftDrawBatch batches[]);
