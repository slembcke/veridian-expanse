/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#define NK_INCLUDE_STANDARD_BOOL
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
	
	bool show_inspector;
	bool show_examples;
} DriftNuklear;

DriftNuklear* DriftNuklearNew(void);
void DriftNuklearFree(DriftNuklear* ctx);

void DriftNuklearSetupGFX(DriftNuklear* ctx, DriftDrawShared* draw_shared);

void DriftNuklearDraw(DriftNuklear* ctx, DriftDraw* draw);
int DriftNuklearHandleEvent(DriftNuklear* nk, SDL_Event *evt);

void DriftNuklearOverview(struct nk_context *ctx);
