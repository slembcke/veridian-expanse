#include <stdio.h>
#include <string.h>

#include "drift_types.h"
#include "drift_util.h"
#include "drift_math.h"
#include "drift_mem.h"
#include "drift_table.h"
#include "drift_map.h"
#include "drift_gfx.h"
#include "drift_gfx_internal.h"
#include "drift_app.h"

#define QOI_IMPLEMENTATION
#define QOI_MALLOC(mem, size) DriftAlloc(mem, size);
#define QOI_FREE(mem, ptr, size) DriftDealloc(mem, ptr, size);
#include "qoi/qoi.h"


void DriftGfxFreeObjects(const DriftGfxDriver* driver, DriftMap* destructors, void* objects[], uint count){
	for(uint i = 0; i < count; i++){
		DriftGfxDestructor* destructor = (DriftGfxDestructor*)DriftMapRemove(destructors, (uintptr_t)objects[i]);
		destructor(driver, objects[i]);
	}
}

void DriftGfxFreeAll(const DriftGfxDriver* driver, DriftMap* destructors){
	for(uint i = 0; i < destructors->table.row_capacity; i++){
		if(DriftMapActiveIndex(destructors, i)){
			((DriftGfxDestructor*)destructors->values[i])(driver, (void*)destructors->keys[i]);
		}
	}
	
	DriftMapDestroy(destructors);
	DriftMapInit(destructors, destructors->table.desc.mem, destructors->table.desc.name, 0);
}

DriftGfxBlendMode DriftGfxBlendModeAlpha = {
	.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
};

DriftGfxBlendMode DriftGfxBlendModePremultipliedAlpha = {
	.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
};

DriftGfxBlendMode DriftGfxBlendModeAdd = {
	.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
};

DriftGfxBlendMode DriftGfxBlendModeMultiply = {
	.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_DST_COLOR, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ZERO,
	.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_DST_COLOR, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ZERO,
};

void DriftGfxRendererInit(DriftGfxRenderer* renderer, DriftGfxVTable vtable){
	(*renderer) = (DriftGfxRenderer){.vtable = vtable};
}

void DriftGfxRendererPrepare(DriftGfxRenderer* renderer, DriftVec2 default_extent, DriftMem* mem){
	DRIFT_ASSERT(mem, "'mem' is NULL.");
	renderer->default_extent = default_extent;
	
	// Reset cursors.
	renderer->first_command = NULL;
	renderer->command_cursor = &renderer->first_command;
	renderer->cursor = renderer->ptr;
	renderer->mem = mem;
}

DriftVec2 DriftGfxRendererDefaultExtent(DriftGfxRenderer* renderer){
	return renderer->default_extent;
}

DriftGfxBufferSlice DriftGfxRendererPushGeometry(DriftGfxRenderer* renderer, const void* ptr, size_t size){
	void* cursor = renderer->cursor.vertex;
	renderer->cursor.vertex = cursor + size;
	DRIFT_ASSERT(renderer->cursor.vertex - renderer->ptr.vertex < DRIFT_GFX_VERTEX_BUFFER_SIZE, "Vertex buffer overflow.");
	if(ptr) memcpy(cursor, ptr, size);
	return (DriftGfxBufferSlice){.ptr = cursor, .binding = {.offset = cursor - renderer->ptr.vertex, .size = size}};
}

DriftGfxBufferSlice DriftGfxRendererPushIndexes(DriftGfxRenderer* renderer, const void* ptr, size_t size){
	void* cursor = renderer->cursor.index;
	renderer->cursor.index = cursor + size;
	DRIFT_ASSERT(renderer->cursor.index - renderer->ptr.index < DRIFT_GFX_INDEX_BUFFER_SIZE, "Index buffer overflow.");
	if(ptr) memcpy(cursor, ptr, size);
	return (DriftGfxBufferSlice){.ptr = cursor, .binding = {.offset = cursor - renderer->ptr.index, .size = size}};
}


DriftGfxBufferSlice DriftGfxRendererPushUniforms(DriftGfxRenderer* renderer, const void* ptr, size_t size){
	void* cursor = renderer->cursor.uniform;
	DriftGfxBufferBinding binding = {.offset = cursor - renderer->ptr.uniform, .size = -(-size & -renderer->uniform_alignment)};
	renderer->cursor.uniform += binding.size;
	DRIFT_ASSERT(renderer->cursor.uniform - renderer->ptr.uniform < DRIFT_GFX_UNIFORM_BUFFER_SIZE, "Uniform buffer overflow.");
	if(ptr) memcpy(cursor, ptr, size);
	return (DriftGfxBufferSlice){.ptr = cursor, .binding = binding};
}

static void DriftGfxRendererPushCommand(DriftGfxRenderer* renderer, DriftGfxCommand* command){
	(*renderer->command_cursor) = command;
	renderer->command_cursor = &command->next;
}

DriftGfxPipelineBindings* DriftGfxRendererPushBindPipelineCommand(DriftGfxRenderer* renderer, DriftGfxPipeline* pipeline){
	DRIFT_ASSERT(pipeline, "Pipeline cannot be NULL");
	
	DriftGfxPipelineBindings* bindings = DriftAlloc(renderer->mem, sizeof(DriftGfxPipelineBindings));
	(*bindings) = (DriftGfxPipelineBindings){};
	
	DriftGfxCommandPipeline* command = DriftAlloc(renderer->mem, sizeof(*command));
	(*command) = (DriftGfxCommandPipeline){.base.func = renderer->vtable.bind_pipeline, .pipeline = pipeline, .bindings = bindings};
	DriftGfxRendererPushCommand(renderer, &command->base);
	
	return bindings;
}

void DriftGfxRendererPushDrawIndexedCommand(DriftGfxRenderer* renderer, DriftGfxBufferBinding index_binding, u32 index_count, u32 instance_count){
	DRIFT_ASSERT(index_binding.offset <= DRIFT_GFX_INDEX_BUFFER_SIZE, "Invalid index array pointer.");
	
	DriftGfxCommandDraw* command = DriftAlloc(renderer->mem, sizeof(*command));
	(*command) = (DriftGfxCommandDraw){
		.base.func = renderer->vtable.draw_indexed,
		.index_binding = index_binding, .index_count = index_count, .instance_count = instance_count,
	};
	
	DriftGfxRendererPushCommand(renderer, &command->base);
}

void DriftGfxRendererPushBindTargetCommand(DriftGfxRenderer* renderer, 	DriftGfxRenderTarget* rt, DriftVec4 clear_color){
	DriftGfxCommandTarget* command = DriftAlloc(renderer->mem, sizeof(*command));
	(*command) = (DriftGfxCommandTarget){
		.base.func = renderer->vtable.bind_target,
		.rt = rt, .clear_color = clear_color,
	};
	
	DriftGfxRendererPushCommand(renderer, &command->base);
}

void DriftGfxRendererPushScissorCommand(DriftGfxRenderer* renderer, DriftAABB2 bounds){
	DriftGfxCommandScissor* command = DriftAlloc(renderer->mem, sizeof(*command));
	(*command) = (DriftGfxCommandScissor){.base.func = renderer->vtable.set_scissor, .bounds = bounds};
	DriftGfxRendererPushCommand(renderer, &command->base);
}

void DriftRendererExecuteCommands(DriftGfxRenderer* renderer){
	DriftGfxRenderState state = {.pipeline = &(DriftGfxPipeline){}};
	
	for(const DriftGfxCommand* command = renderer->first_command; command; command = command->next){
		command->func(renderer, command, &state);
	}
}
