/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "microui/microui.h"

#include "drift_game.h"

#define HIGHLIGHT "{#00FFFFFF}"

const DriftScan DRIFT_SCANS[_DRIFT_SCAN_COUNT] = {
	[DRIFT_SCAN_NONE] = {.name = "none"},
	[DRIFT_SCAN_VIRIDIUM] = {
		.name = "Viridium", .radius = 12,
		.description = "A versatile metal that is ideal for industrial fabrication. Its name derives from the deep green color of its most common ore, viridium oxide. In its metallic form, it is often anodized with a violet color for durability."
	},
	[DRIFT_SCAN_BORONITE] = {
		.name = "Boronite", .radius = 12,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_RADONITE] = {
		.name = "Radonite", .radius = 12,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_METRIUM] = {
		.name = "Metrium", .radius = 12,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_LUMIUM] = {
		.name = "Lumium", .radius = 8,
		.description = "A crystalline semiconductor that is incredibly efficient at converting light to and from other sources of energy. Its discovery could revolutionize energy back on the homeworld. Can be obtained by destroying "HIGHLIGHT"GLOW BUGS"DRIFT_TEXT_WHITE"."
	},
	[DRIFT_SCAN_FLOURON] = {
		.name = "Flouron", .radius = 8,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_FUNGICITE] = {
		.name = "Fungicite", .radius = 8,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_MORPHITE] = {
		.name = "Morphite", .radius = 8,
		.description = "TODO need some description text here.",
	},
	[DRIFT_SCAN_SCRAP] = {
		.name = "Scrap", .radius = 10,
		.description = "Scrap metal remnants of a bio-mechanoid. Useful as a fabrication material, and can be obtained by destroying large bio mechanoids such as "HIGHLIGHT"hive workers"DRIFT_TEXT_WHITE".",
	},
	[DRIFT_SCAN_COPPER] = {
		.name = "Copper", .radius = 12,
		.description = "A conductive metal useful for constructing high power systems. Can be obtained from hives located in the light biome.",
	},
	[DRIFT_SCAN_COPPER_DEPOSIT] = {
		.name = "Copper Deposit", .radius = 12,
		.description = "A large deposit of copper.\n"HIGHLIGHT"NOTE: An industrial crusher is required to harvest it.",
	},
	[DRIFT_SCAN_SILVER] = {
		.name = "Silver", .radius = 12,
		.description = "A highly conductive metal useful for fabricating advanced electronics.",
	},
	[DRIFT_SCAN_GOLD] = {
		.name = "Gold", .radius = 12,
		.description = "A very stable metal useful for fabricating advanced technologies.",
	},
	[DRIFT_SCAN_GRAPHENE] = {
		.name = "Graphene", .radius = 12,
		.description = "Carbon nanomaterial with incredible electrical and structural properties. Required component of all next-gen technologies.",
	},
	[DRIFT_SCAN_GLOW_BUG] = {
		.name = "Glow Bug", .radius = 12,
		.description = "A harmless biomechanical insectoid found abundantly in the Bright Caverns biome. Glow bugs accumulate ultra pure "HIGHLIGHT"LUMIUM"DRIFT_TEXT_WHITE" in their abdomens which can be obtained by destroying them.",
	},
	[DRIFT_SCAN_HIVE_WORKER] = {
		.name = "Hive Worker", .radius = 14,
		.description =
			"A mostly harmless biomechanical insectoid. Accumulates resources and stores them in their " HIGHLIGHT "HIVE" DRIFT_TEXT_WHITE ". Drops "HIGHLIGHT"SCRAP"DRIFT_TEXT_WHITE" when destroyed."
	},
	[DRIFT_SCAN_HIVE_FIGHTER] = {
		.name = "Hive Fighter", .radius = 14,
		.description = "An aggressive biomechanical insectoid usually found near a " HIGHLIGHT "HIVE" DRIFT_TEXT_WHITE " Drops "HIGHLIGHT"SCRAP"DRIFT_TEXT_WHITE" when destroyed.",
	},
	[DRIFT_SCAN_HIVE] = {
		.name = "Hive", .radius = 48,
		.description = "A biomechanical insectoid hive. Hive workers accumulate precious resources and store them within the hive. A large deposit of " HIGHLIGHT "CONDUCTIVE METAL" DRIFT_TEXT_WHITE " can be detected within. You'll need to break it open to harvest resources within.",
	},
	[DRIFT_SCAN_HIVE_POD] = {
		.name = "Hive Pod", .radius = 48,
		.description = "A protective extrusion of the main hive. In addition to discarging blasts of energy to deter predators, it also generates a defensive field around the hive.",
	},
	[DRIFT_SCAN_TRILOBYTE_LARGE] = {
		.name = "Trilobyte", .radius = 24,
		.description = "A dangerous creature without an interesting scan entry.",
	},
	[DRIFT_SCAN_NAUTILUS_HEAVY] = {
		.name = "Alpha Nautilus", .radius = 24,
		.description = "A large unfinished creature.",
	},
	[DRIFT_SCAN_CONSTRUCTION_SKIFF] = {
		.name = "Construction Skiff", .radius = 12,
		.description = "A damaged construction skiff with an onboard industrial fabricator and power reactor. Don't get to attached to it though. It's just a placeholder asset after all.",
	},
	[DRIFT_SCAN_POWER_NODE] = {
		.name = "Power Nodes", .radius = 12,
		.description = "An industrial scale power and data transmision node. They allow for an efficient centralized infrastructure and allow a high power to weight ratio on connected machinery.",
	},
	[DRIFT_SCAN_DRONE] = {
		.name = "Task Drone",
		.description = "Work smarter, not harder by delegating basic tasks to drones.",
	},
	[DRIFT_SCAN_OPTICS] = {
		.name = "Optical Parts",
		.description = "Optical parts are used to build upgrades that need to manipulate light.",
	},
	[DRIFT_SCAN_POWER_SUPPLY] = {
		.name = "Power Supply",
		.description = "Electrical power supply circuit used to build upgrades requiring up to 20 kW of continuous power.",
	},
	[DRIFT_SCAN_HEADLIGHT] = {
		.name = "Lumium Lights",
		.description = "See more of the world with the power of Lumium.",
		.usage = "Press {@LIGHT} to toggle headlight.",
	},
	[DRIFT_SCAN_AUTOCANNON] = {
		.name = "Autocannon",
		.description = "Upgrades your cannon to fire a high speed burst of projectiles.",
		.usage = "Press {@FIRE} to fire.",
	},
	[DRIFT_SCAN_ZIP_CANNON] = {
		.name = "Zip Cannon",
		.description = "Placeholder",
	},
	[DRIFT_SCAN_LASER] = {
		.name = "Mining Laser",
		.description = "A high powered mining laser. Capable of cutting through solid rock in mere seconds.",
		.usage = "Press {@LASER} to use the laser.",
	},
	[DRIFT_SCAN_FAB_RADIO] = {
		.name = "Fab Radio",
		.description = "A radio module that allows remote control of your fabricator. You can start research or build items while out exploring.",
		.usage = "Press {@MAP} and then select \"Craft\".",
	},
	[DRIFT_SCAN_SHIELD_L2] = {
		.name = "Super Shield",
		.description = "A high powered shield module that can take absorb double the damage.",
	},
	[DRIFT_SCAN_SHIELD_L3] = {
		.name = "Mirror Shield",
		.description = "Reflect your cares away... (TODO)",
	},
	[DRIFT_SCAN_CARGO_L2] = {
		.name = "Cargo L2",
		.description = "Increase your cargo capacity by 50%.",
	},
	[DRIFT_SCAN_CARGO_L3] = {
		.name = "Cargo L3",
		.description = "Double your cargo capacity",
	},
	[DRIFT_SCAN_NODES_L2] = {
		.name = "Nodes L2",
		.description = "Increase your node capacity by 50%.",
	},
	[DRIFT_SCAN_NODES_L3] = {
		.name = "Nodes L3",
		.description = "Double your node capacity.",
	},
	[DRIFT_SCAN_STORAGE_L2] = {
		.name = "Storage L2",
		.description = "Increase your storage capacity by 50%.",
	},
	[DRIFT_SCAN_STORAGE_L3] = {
		.name = "Storage L3",
		.description = "Double your storage capacity.",
	},
};

typedef struct {
	DriftGameState* state;
	mu_Context* mu;
	mu_Container* win;
	DriftScanType select, selected;
} RowContext;

static void row(RowContext* ctx, DriftScanType type){
	mu_Context* mu = ctx->mu;
	mu_Container* win = ctx->win;
	int ROW[] = {-1};
	
	if(ctx->select && ctx->select == type) win->ve_row = win->ve_inc, ctx->select = DRIFT_SCAN_NONE;
	if(win->ve_row == win->ve_inc) ctx->selected = type;
	
	mu_layout_row(mu, 1, ROW, 16);
	mu_begin_group(mu, 0);
	if(ctx->state->scan_progress[type] == 1){
		mu_layout_row(mu, 1, ROW, 0);
		mu_labelf(mu, "{#CBCBCA95}%s", DRIFT_SCANS[type].name ?: "!!NO NAME!!");
	} else {
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "{#8418185E}(undiscovered)");
	}
	mu_end_group(mu);
}

void DriftScanUI(mu_Context* mu, DriftDraw* draw, DriftScanType* select, DriftUIState* ui_state){
	DriftVec2 extents = draw->internal_extent;
	mu_Vec2 size = {400, 200};
	
	static const char* TITLE = "Scans";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->rect = (mu_Rect){(int)(extents.x - size.x)/2, (int)(extents.y - size.y)/2, size.x, size.y};
	win->open = (*ui_state == DRIFT_UI_STATE_SCAN);
	
	RowContext ctx = {.state = draw->state, .mu = mu, .win = win, .select = *select};
	*select = DRIFT_SCAN_NONE;
	
	static const int ROW[] = {-1};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_bring_to_front(mu, win);
		
		mu_layout_row(mu, 2, (int[]){-260, -1}, -1);
		
		mu_begin_panel(mu, "list");
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Technology:");
		row(&ctx, DRIFT_SCAN_CONSTRUCTION_SKIFF);
		row(&ctx, DRIFT_SCAN_POWER_NODE);
		row(&ctx, DRIFT_SCAN_HIVE);
		row(&ctx, DRIFT_SCAN_HIVE_POD);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Light Creatures:");
		row(&ctx, DRIFT_SCAN_GLOW_BUG);
		row(&ctx, DRIFT_SCAN_HIVE_WORKER);
		row(&ctx, DRIFT_SCAN_HIVE_FIGHTER);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Light Resources:");
		row(&ctx, DRIFT_SCAN_VIRIDIUM);
		row(&ctx, DRIFT_SCAN_LUMIUM);
		row(&ctx, DRIFT_SCAN_SCRAP);
		row(&ctx, DRIFT_SCAN_COPPER);
		row(&ctx, DRIFT_SCAN_COPPER_DEPOSIT);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Cryo Creatures:");
		row(&ctx, DRIFT_SCAN_NAUTILUS_HEAVY);
		row(&ctx, DRIFT_SCAN_TRILOBYTE_LARGE);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Cryo Resources:");
		row(&ctx, DRIFT_SCAN_BORONITE);
		row(&ctx, DRIFT_SCAN_FLOURON);
		row(&ctx, DRIFT_SCAN_SILVER);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Radio Resources:");
		row(&ctx, DRIFT_SCAN_RADONITE);
		row(&ctx, DRIFT_SCAN_FUNGICITE);
		row(&ctx, DRIFT_SCAN_GOLD);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Dark Resources:");
		row(&ctx, DRIFT_SCAN_METRIUM);
		row(&ctx, DRIFT_SCAN_MORPHITE);
		row(&ctx, DRIFT_SCAN_GRAPHENE);
		mu_end_panel(mu);
		
		// Draw item details.
		mu_layout_begin_column(mu);
		const DriftScan* scan = DRIFT_SCANS + ctx.selected;
		const char* name = scan->name ?: "!!NO NAME!!";
		const char* description = scan->description ?: "!!NO DESCRIPTION!!";
		
		if(draw->state->scan_progress[ctx.selected] < 1){
			scan = DRIFT_SCANS + DRIFT_SCAN_NONE;
			name = "Undiscovered Item";
			description = "You haven't discovered this item yet. Keep researching items using {@SCAN} to learn more.";
		}
		
		// TODO this needs to get cleaned up.
		static const char* LOADED;
		if(LOADED != scan->name){
			LOADED = scan->name;
			char* filename = DriftSMPrintf(draw->mem, "gfx/scans/%s.qoi", scan->name);
			
			// Normalize file name.
			for(char* c = filename; *c; c++){
				if('A' <= *c && *c <='Z') *c += 0x20;
				if(*c == ' ') *c = '_';
			}
			DRIFT_LOG("loading scan image '%s'", filename);
			
			DriftImage img = DriftAssetLoadImage(draw->mem, filename);
			img.pixels = DriftRealloc(draw->mem, img.pixels, img.w*img.h*4, DRIFT_ATLAS_SIZE*DRIFT_ATLAS_SIZE*4);
			
			const DriftGfxDriver* driver = draw->shared->driver;
			uint queue = tina_job_switch_queue(draw->job, DRIFT_JOB_QUEUE_GFX);
			driver->load_texture_layer(driver, draw->shared->atlas_texture, DRIFT_ATLAS_SCAN, img.pixels);
			tina_job_switch_queue(draw->job, queue);
		}
		
		mu_layout_row(mu, 1, (int[]){256}, 64); {
			mu_Rect r = mu_layout_next(mu);
			mu_draw_icon(mu, -DRIFT_SPRITE_SCAN_IMAGE, r, (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, 0);
		mu_labelf(mu, "%s:", name);
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_Style* _style = mu->style, style = *_style;
		mu->style = &style;
		style.colors[MU_COLOR_TEXT] = (mu_Color){0x80, 0x80, 0x80, 0xFF};
		mu_text(mu, description);
		mu->style = _style;
		mu_layout_end_column(mu);
		
		DriftUICloseIndicator(mu, win);
		if(!win->open) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_window(mu);
	}
}
