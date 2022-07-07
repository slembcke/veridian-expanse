#pragma once

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#include "nuklear/nuklear.h"

typedef struct DriftNuklear {
	struct nk_context nk;
	struct nk_user_font font;
	
	struct nk_buffer commands, vertexes, indexes;
	struct nk_convert_config convert_config;
	DriftGfxPipeline* pipeline;
	
	bool show_info;
	bool show_examples;
} DriftNuklear;

DriftNuklear* DriftNuklearNew(void);
void DriftNuklearFree(DriftNuklear* ctx);

void DriftNuklearSetupGFX(DriftNuklear* ctx, DriftDrawShared* draw_shared);

void DriftNuklearDraw(DriftNuklear* ctx, DriftDraw* draw);
int DriftNuklearHandleEvent(DriftNuklear* nk, SDL_Event *evt);

void DriftNuklearOverview(struct nk_context *ctx);
