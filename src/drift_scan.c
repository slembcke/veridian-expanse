#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "microui/microui.h"

#include "drift_game.h"

#define HIGHLIGHT "{#00FFFFFF}"

const DriftScan DRIFT_SCANS[_DRIFT_SCAN_COUNT] = {
	[DRIFT_SCAN_NONE].name = "none",
	[DRIFT_SCAN_VIRIDIUM] = {
		.name = "Viridium", .radius = 8,
		.description =
			"A versatile metal that is ideal for industrial fabrication. "
			"Its name derives from the deep green color of its most common ore, viridium oxide. "
			"In its metalic form, it is often anodized with a violet color for durability."
	},
	[DRIFT_SCAN_LUMIUM] = {
		.name = "Lumium", .radius = 8,
		.description =
			"A crystaline semiconductor that is incredibly efficient at converting light to and from other sources of energy. "
			"Its discovery could revolutionize energy back on the homeworld. "
			"Can be obtained by destroying "HIGHLIGHT"GLOW BUGS"DRIFT_TEXT_WHITE"."
	},
	[DRIFT_SCAN_SCRAP] = {
		.name = "Scrap", .radius = 10,
		.description = "Broken parts. Useful as a fabrication material.",
	},
	[DRIFT_SCAN_GLOW_BUG] = {
		.name = "Glow Bug", .radius = 12,
		.description =
			"A harmless biomechanical insectoid found abundantly in the Bright Caverns biome. "
			"Glow bugs accumulate ultra pure "HIGHLIGHT"LUMIUM"DRIFT_TEXT_WHITE" in their abdomens which can be obtained by destroying them."
	},
	[DRIFT_SCAN_HIVE_WORKER] = {
		.name = "Hive Worker", .radius = 14,
		.description =
			"A mostly harmless biomechanical insectoid. Accumulates precious resources and stores them in their "HIGHLIGHT"HIVE"DRIFT_TEXT_WHITE". "
			"Drops "HIGHLIGHT"SCRAP"DRIFT_TEXT_WHITE" when destroyed."
	},
	[DRIFT_SCAN_HIVE_FIGHTER] = {
		.name = "Hive Worker", .radius = 14,
		.description =
			"An aggressive biomechanical insectoid. Needs more description."
	},
	[DRIFT_SCAN_FACTORY] = {
		.name = "Fabricator", .radius = 12, .offset = {30, 10}, .interactive = true,
		.description =
			"An industrial fabricator that can construct tools and other items from raw materials. "
			"Press {@SCAN} to activate the fabricator interface with your "HIGHLIGHT"SCANNER"DRIFT_TEXT_WHITE".",
	},
	[DRIFT_SCAN_CRASHED_SHIP] = {
		.name = "Crashed Ship", .radius = 12,
		.description = "Your broken ship. You'll need to fix this.",
	},
	[DRIFT_SCAN_POWER_NODE] = {
		.name = "Power Node", .radius = 12, .interactive = true,
		.description =
			"An industrial scale power and data transmision module. "
			"They allow for an efficient centralized infrastructure and allow a "
			"high power to weight ratio on connected machinery such as your idustrial pod."
	},
	
	// TODO should I include descriptions for craftables?
	// [DRIFT_SCAN_OPTICS] = {
	// 	.name = "Optical Parts",
	// 	.description = "Used to build stuff.",
	// },
	// [DRIFT_SCAN_HEADLIGHT] = {
	// 	.name = "Lumium Headlight",
	// 	.description = "See more with the power of Lumium.",
	// },
	// [DRIFT_SCAN_AUTOCANNON] = {
	// 	.name = "Autocannon",
	// 	.description = "Shoots faster than a rusty cannon.",
	// },
	// [DRIFT_SCAN_LASER] = {
	// 	.name = "Mining Laser",
	// 	.description = "Mines 50% faster than an autospoon.",
	// },
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
	if(ctx->state->scan_progress[type] < 1){
		mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "{#60300030}(undiscovered)");
		mu_end_group(mu);
	} else {
		mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, ROW, 0);
		mu_labelf(mu, "{#C0C0C0FF}%s", DRIFT_SCANS[type].name ?: "!!NO NAME!!");
		mu_end_group(mu);
	}
}

void DriftScanUI(DriftDraw* draw, DriftUIState* ui_state, DriftScanType select){
	mu_Context* mu = draw->ctx->mu;
	DriftVec2 extents = draw->internal_extent;
	mu_Vec2 size = {400, 200};
	
	static const char* TITLE = "Scans";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->rect = (mu_Rect){(int)(extents.x - size.x)/2, (int)(extents.y - size.y)/2, size.x, size.y};
	win->open = (*ui_state == DRIFT_UI_STATE_SCAN);
	
	RowContext ctx = {.state = draw->state, .mu = mu, .win = win, .select = select};
	static const int ROW[] = {-1};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 2, (int[]){-1}, 18);
		mu_begin_box(mu, MU_COLOR_GROUPBG, 0);
		mu_layout_row(mu, 2, (int[]){-60, -1}, -1);
		mu_label(mu, "Scans:");
		if(mu_button(mu, "Close {@CANCEL}") || mu->key_down & MU_KEY_ESCAPE) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_box(mu);
		
		mu_layout_row(mu, 2, (int[]){-260, -1}, -1);
		
		// Draw scans list
		mu_begin_panel(mu, "list");
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Technology:");
		row(&ctx, DRIFT_SCAN_POWER_NODE);
		row(&ctx, DRIFT_SCAN_CRASHED_SHIP);
		row(&ctx, DRIFT_SCAN_FACTORY);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Light Creatures:");
		row(&ctx, DRIFT_SCAN_GLOW_BUG);
		row(&ctx, DRIFT_SCAN_HIVE_WORKER);
		
		mu_layout_row(mu, 1, ROW, 0);
		mu_label(mu, "Light Resources:");
		row(&ctx, DRIFT_SCAN_VIRIDIUM);
		row(&ctx, DRIFT_SCAN_LUMIUM);
		row(&ctx, DRIFT_SCAN_SCRAP);
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
			
			DriftImage img = DriftAssetLoadImage(DriftSystemMem, filename);
			img.pixels = DriftRealloc(DriftSystemMem, img.pixels, img.w*img.h*4, DRIFT_ATLAS_SIZE*DRIFT_ATLAS_SIZE*4);
			
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
		
		mu_check_close(mu);
		if(!win->open) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_window(mu);
	}
}
