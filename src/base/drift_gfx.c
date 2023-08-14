/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>

#include "drift_base.h"
#include "drift_gfx_internal.h"

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
	
	DriftGfxPipelineBindings* bindings = DRIFT_COPY(renderer->mem, ((DriftGfxPipelineBindings){}));
	DriftGfxRendererPushCommand(renderer, &DRIFT_COPY(renderer->mem, ((DriftGfxCommandPipeline){
		.base.func = renderer->vtable.bind_pipeline, .pipeline = pipeline, .bindings = bindings
	}))->base);
	
	return bindings;
}

void DriftGfxRendererPushDrawIndexedCommand(DriftGfxRenderer* renderer, DriftGfxBufferBinding index_binding, u32 index_count, u32 instance_count){
	DRIFT_ASSERT(index_binding.offset <= DRIFT_GFX_INDEX_BUFFER_SIZE, "Invalid index array pointer.");
	DriftGfxRendererPushCommand(renderer, &DRIFT_COPY(renderer->mem, ((DriftGfxCommandDraw){
		.base.func = renderer->vtable.draw_indexed,
		.index_binding = index_binding, .index_count = index_count, .instance_count = instance_count,
	}))->base);
}

void DriftGfxRendererPushBindTargetCommand(DriftGfxRenderer* renderer, 	DriftGfxRenderTarget* rt, DriftVec4 clear_color){
	DriftGfxRendererPushCommand(renderer, &DRIFT_COPY(renderer->mem, ((DriftGfxCommandTarget){
		.base.func = renderer->vtable.bind_target,
		.rt = rt, .clear_color = clear_color,
	}))->base);
}

void DriftGfxRendererPushScissorCommand(DriftGfxRenderer* renderer, DriftAABB2 bounds){
	DriftGfxRendererPushCommand(renderer, &DRIFT_COPY(renderer->mem, ((DriftGfxCommandScissor){
		.base.func = renderer->vtable.set_scissor, .bounds = bounds
	}))->base);
}

void DriftRendererExecuteCommands(DriftGfxRenderer* renderer){
	DriftGfxRenderState state = {.pipeline = &(DriftGfxPipeline){}};
	
	for(const DriftGfxCommand* command = renderer->first_command; command; command = command->next){
		command->func(renderer, command, &state);
	}
}
