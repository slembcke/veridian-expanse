#include <stdlib.h>
#include <string.h>

#include "drift_game.h"
#include "microui/microui.h"

const DriftItem DRIFT_ITEMS[_DRIFT_ITEM_COUNT] = {
	[DRIFT_ITEM_NONE] = {.name = "none"},
	
	// Scrap
	[DRIFT_ITEM_SCRAP] = {.name = "Scrap", .scan = DRIFT_SCAN_SCRAP, .is_cargo = true},
	[DRIFT_ITEM_ADVANCED_SCRAP] = {.name = "Advanced Scrap", .is_cargo = true},
	
	// Ore
	[DRIFT_ITEM_VIRIDIUM] = {.name = "Viridium", .scan = DRIFT_SCAN_VIRIDIUM, .is_cargo = true},
	[DRIFT_ITEM_BORONITE] = {.name = "Boronite", .is_cargo = true},
	[DRIFT_ITEM_RADONITE] = {.name = "Radonite", .is_cargo = true},
	[DRIFT_ITEM_METRIUM] = {.name = "Metrium", .is_cargo = true},
	
	// Rare
	[DRIFT_ITEM_COPPER] = {.name = "Copper", .is_cargo = true},
	[DRIFT_ITEM_SILVER] = {.name = "Silver", .is_cargo = true},
	[DRIFT_ITEM_GOLD] = {.name = "Gold", .is_cargo = true},
	[DRIFT_ITEM_GRAPHENE] = {.name = "Graphene", .is_cargo = true},
	
	// Biomech
	[DRIFT_ITEM_LUMIUM] = {.name = "Lumium", .scan = DRIFT_SCAN_LUMIUM, .is_cargo = true},
	[DRIFT_ITEM_FLOURON] = {.name = "Flouron", .is_cargo = true},
	[DRIFT_ITEM_FUNGICITE] = {.name = "Fungicite", .is_cargo = true},
	[DRIFT_ITEM_MORPHITE] = {.name = "Morphite", .is_cargo = true},
	
	// Intermediate
	[DRIFT_ITEM_POWER_SUPPLY] = {.name = "Power Supply"},
	[DRIFT_ITEM_OPTICS] = {.name = "Optical Parts", .scan = DRIFT_SCAN_OPTICS},
	// [DRIFT_ITEM_CPU] = {.name = "CPU"},
	
	// Tools
	[DRIFT_ITEM_HEADLIGHT] = {.name = "Lumium Lights", .scan = DRIFT_SCAN_HEADLIGHT, .can_only_have_one = true},
	[DRIFT_ITEM_CANNON] = {.name = "Cannon", .can_only_have_one = true},
	[DRIFT_ITEM_MINING_LASER] = {.name = "Mining Laser", .scan = DRIFT_SCAN_LASER, .can_only_have_one = true},
	[DRIFT_ITEM_SCANNER] = {.name = "Scanner", .can_only_have_one = true},
	[DRIFT_ITEM_DRONE_CONTROLLER] = {.name = "Drone Controller", .can_only_have_one = true},
	
	// Upgrades
	[DRIFT_ITEM_AUTOCANNON] = {.name = "Autocannon", .scan = DRIFT_SCAN_AUTOCANNON, .can_only_have_one = true},
	[DRIFT_ITEM_ZIP_CANNON] = {.name = "Zip Cannon", .can_only_have_one = true},
	[DRIFT_ITEM_HEAT_EXCHANGER] = {.name = "Heat Exchanger", .can_only_have_one = true},
	[DRIFT_ITEM_THERMAL_CONDENSER] = {.name = "Thermoelectric Condenser", .can_only_have_one = true},
	[DRIFT_ITEM_RADIO_PLATING] = {.name = "Radiation Sheilding", .can_only_have_one = true},
	[DRIFT_ITEM_MIRROR_PLATING] = {.name = "Mirror Sheilding", .can_only_have_one = true},
	[DRIFT_ITEM_SPECTROMETER] = {.name = "Spectrometer", .can_only_have_one = true},
	[DRIFT_ITEM_SMELTING_MODULE] = {.name = "Smelting Module", .can_only_have_one = true},
	
	// Consumables
	[DRIFT_ITEM_POWER_NODE] = {.name = "Power Node", .scan = DRIFT_SCAN_POWER_NODE, .is_cargo = true},
	[DRIFT_ITEM_FUNGAL_NODE] = {.name = "Fungal Node", .is_cargo = true},
	[DRIFT_ITEM_METRIUM_NODE] = {.name = "Metrium Node", .is_cargo = true},
	[DRIFT_ITEM_DRONE] = {.name = "Drone"},
};

const DriftCraftableItem DRIFT_CRAFTABLES[] = {
	// {.item = DRIFT_ITEM_POWER_SUPPLY, .ingredients = {
	// 	{.type = DRIFT_ITEM_SCRAP, .count = 2},
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
	// }},
	[DRIFT_ITEM_OPTICS].ingredients = {
		{.type = DRIFT_ITEM_SCRAP, .count = 1},
		{.type = DRIFT_ITEM_LUMIUM, .count = 2},
		{.type = DRIFT_ITEM_VIRIDIUM, .count = 8},
	},
	// [DRIFT_ITEM_CPU] = {{
	// 	{.type = DRIFT_ITEM_SCRAP, .count = 1},
	// }},
	[DRIFT_ITEM_POWER_NODE].makes = 10,
	[DRIFT_ITEM_POWER_NODE].ingredients = {
		{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
		{.type = DRIFT_ITEM_LUMIUM, .count = 1},
		{.type = DRIFT_ITEM_SCRAP, .count = 1},
	},
	// {.item = DRIFT_ITEM_FUNGAL_NODE, {
	// 	{.type = DRIFT_ITEM_POWER_SUPPLY, .count = 1},
	// 	{.type = DRIFT_ITEM_LUMIUM, .count = 1},
	// }},
	// {.item = DRIFT_ITEM_METRIUM_NODE, {
	// 	{.type = DRIFT_ITEM_POWER_SUPPLY, .count = 1},
	// 	{.type = DRIFT_ITEM_LUMIUM, .count = 1},
	// }},
	
	[DRIFT_ITEM_HEADLIGHT].ingredients = {
		{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
		{.type = DRIFT_ITEM_LUMIUM, .count = 5},
	},
	// {.item = DRIFT_ITEM_CANNON, .ingredients = {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
	// }},
	[DRIFT_ITEM_AUTOCANNON].ingredients = {
		{.type = DRIFT_ITEM_VIRIDIUM, .count = 6},
		{.type = DRIFT_ITEM_SCRAP, .count = 3},
	},
	[DRIFT_ITEM_MINING_LASER].ingredients = {
		{.type = DRIFT_ITEM_LUMIUM, .count = 10},
		{.type = DRIFT_ITEM_SCRAP, .count = 2},
		// {.type = DRIFT_ITEM_POWER_SUPPLY, .count = 1},
		{.type = DRIFT_ITEM_OPTICS, .count = 2},
	},
	// {.item = DRIFT_ITEM_SCANNER, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_DRONE_CONTROLLER, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_ZIP_CANNON, {
	// 	{.type = DRIFT_ITEM_SCRAP, .count = 12},
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 40},
	// }},
	// {.item = DRIFT_ITEM_HEAT_EXCHANGER, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_THERMAL_CONDENSER, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_RADIO_PLATING, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_MIRROR_PLATING, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_SPECTROMETER, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	// {.item = DRIFT_ITEM_SMELTING_MODULE, {
	// 	{.type = DRIFT_ITEM_VIRIDIUM, .count = 5},
	// }},
	{},
};

static DriftEntity pnode_make(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_SCAN_POWER_NODE;
	
	return e;
}

static void pnode_draw(DriftDraw* draw, DriftVec2 pos){
	bool flash = DriftWaveSaw(draw->nanos, 3) < 0.75f;
	
	// TODO draw lines in parts to highlight min dist?
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(draw->state, pos, draw->mem, DRIFT_POWER_BEAM_RADIUS);
	DRIFT_ARRAY_FOREACH(info.nodes, node){
		if(node->is_too_close){
			// Flash nodes too close in red.
			if(flash){
				DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, node->pos, {1.5}, DRIFT_RGBA8_RED}));
				DriftDrawText(draw, &draw->overlay_sprites, (DriftAffine){1, 0, 0, 1, node->pos.x + 12, node->pos.y - 4}, DRIFT_VEC4_RED, "TOO CLOSE");
			}
		} else if(node->blocked_at < 1){
			// TODO cleanup
			// Draw blocked nodes in orange.
			DriftVec2 p0 = node->pos, p1 = DriftVec2Lerp(p0, pos, node->blocked_at);
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, p1, {1.5}, {0x80, 0x00, 0x00, 0x80}}));
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){p1, node->pos, {1.5}, {0x80, 0x40, 0x00, 0x80}}));
			
			float pulse = 1 - fabsf(DriftWaveComplex(draw->nanos, 1).x);
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){p0, DriftVec2Lerp(p0, pos, pulse*node->blocked_at), {DRIFT_POWER_BEAM_RADIUS, DRIFT_POWER_BEAM_RADIUS - 1.5f}, {0x80, 0x40, 0x00, 0x80}}));
		} else {
			// Draw good links in green.
			u8 v = info.node_can_connect ? 0xFF : 0x20;
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, node->pos, {1.5}, {0, v, 0, v}}));
		}
	}
	
	if(info.node_can_connect){
		DriftDrawText(draw, &draw->overlay_sprites, (DriftAffine){1, 0, 0, 1, pos.x + 12, pos.y - 4}, DRIFT_VEC4_GREEN, "VALID");
	} else {
		DriftDrawText(draw, &draw->overlay_sprites, (DriftAffine){1, 0, 0, 1, pos.x + 12, pos.y - 4}, DRIFT_VEC4_RED, "INVALID");
	}
}

static void pnode_grab(DriftUpdate* update, DriftEntity e){
	DriftComponentRemove(&update->state->power_nodes.c, e);
	for(uint i = 0; i < 1; i++) DriftComponentRemove(&update->state->flow_maps[i].c, e);
	// update->ctx->debug.pause = true;
}

void DriftPowerNodeActivate(DriftGameState* state, DriftEntity e, DriftMem* mem){
	uint idx = DriftComponentFind(&state->power_nodes.c, e);
	
	// Create the component if it doesn't already have one.
	if(idx == 0){
		idx = DriftComponentAdd(&state->power_nodes.c, e);
		
		uint transform_idx = DriftComponentFind(&state->transforms.c, e);
		state->power_nodes.position[idx] = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	}
	
	DriftVec2 pos = state->power_nodes.position[idx];
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, mem, DRIFT_POWER_BEAM_RADIUS);
	if(info.node_can_connect){
		// Make connections to other nodes.
		DRIFT_ARRAY_FOREACH(info.nodes, node_info){
			// Don't connect to itself.
			if(e.id == node_info->e.id) continue;
			
			if(node_info->node_can_connect){
				uint edge_idx = DriftTablePushRow(&state->power_edges.t);
				state->power_edges.edge[edge_idx] = (DriftPowerNodeEdge){.e0 = e, .e1 = node_info->e, .p0 = pos, .p1 = node_info->pos};
			}
		}
		
		// // Try activating nearby inactive nodes.
		// DRIFT_ARRAY_FOREACH(info.nodes, node_info){
		// 	if(node_info->e.id != e.id && !state->power_nodes.active[DriftComponentFind(&state->power_nodes.c, node_info->e)]){
		// 		DriftPowerNodeActivate(state, node_info->e, mem);
		// 	}
		// }
	}
}

static void pnode_drop(DriftUpdate* update, DriftEntity e){
	DriftGameState* state = update->state;
	uint transform_idx = DriftComponentFind(&state->transforms.c, e);
	DriftVec2 pos = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	
	DriftPowerNodeActivate(update->state, e, update->mem);
}

typedef DriftEntity DriftItemMakeFunc(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel);
typedef void DriftItemDrawPickupFunc(DriftDraw* draw, DriftVec2 pos);
typedef void DriftItemPickupFunc(DriftUpdate* update, DriftEntity e);

static DriftItemMakeFunc item_make_generic;

static const struct {
	DriftItemMakeFunc* make;
	DriftItemDrawPickupFunc* draw;
	DriftItemPickupFunc* grab;
	DriftItemPickupFunc* drop;
	
	uint frame, light_frame;
	float shiny, light_size;
	DriftVec4 light_color;
	DriftLight light;
} DRIFT_PICKUP_ITEMS[_DRIFT_ITEM_COUNT] = {
	[DRIFT_ITEM_SCRAP] = {.make = item_make_generic, .frame = DRIFT_SPRITE_SCRAP, .shiny = 0.7f},
	[DRIFT_ITEM_ADVANCED_SCRAP] = {.make = item_make_generic, .frame = DRIFT_SPRITE_ADVANCED_SCRAP, .shiny = 0.7f},
	
	[DRIFT_ITEM_VIRIDIUM] = {.make = item_make_generic, .frame = DRIFT_SPRITE_VIRIDIUM},
	[DRIFT_ITEM_BORONITE] = {.make = item_make_generic, .frame = DRIFT_SPRITE_BORON},
	[DRIFT_ITEM_RADONITE] = {.make = item_make_generic, .frame = DRIFT_SPRITE_RADONITE},
	[DRIFT_ITEM_METRIUM] = {.make = item_make_generic, .frame = DRIFT_SPRITE_METRIUM},
	
	[DRIFT_ITEM_LUMIUM] = {
		.make = item_make_generic, .frame = DRIFT_SPRITE_LUMIUM, 
		.light_frame = DRIFT_SPRITE_LIGHT_RADIAL, .light_color = (DriftVec4){{0.24f, 0.16f, 0.04f, 2.00f}}, .light_size = 75,
	},
	[DRIFT_ITEM_FLOURON] = {.make = item_make_generic, .frame = DRIFT_SPRITE_FLOURON},
	[DRIFT_ITEM_FUNGICITE] = {.make = item_make_generic, .frame = DRIFT_SPRITE_FUNGICITE},
	[DRIFT_ITEM_MORPHITE] = {.make = item_make_generic, .frame = DRIFT_SPRITE_MORPHITE},
	
	[DRIFT_ITEM_COPPER] = {.make = item_make_generic, .frame = DRIFT_SPRITE_COPPER, .shiny = 0.5f},
	[DRIFT_ITEM_SILVER] = {.make = item_make_generic, .frame = DRIFT_SPRITE_SILVER, .shiny = 1},
	[DRIFT_ITEM_GOLD] = {.make = item_make_generic, .frame = DRIFT_SPRITE_GOLD, .shiny = 1},
	[DRIFT_ITEM_GRAPHENE] = {.make = item_make_generic, .frame = DRIFT_SPRITE_GRAPHENE, .shiny = 0.5f},
	
	[DRIFT_ITEM_POWER_NODE] = {.make = pnode_make, .draw = pnode_draw, .grab = pnode_grab, .drop = pnode_drop, .frame = DRIFT_SPRITE_POWER_NODE},
};

static DriftEntity item_make_generic(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_ITEMS[type].scan;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	state->bodies.velocity[body_idx] = DriftVec2Mul(vel, 0.25f);
	float a = pos.x;
	state->bodies.rotation[body_idx] = (DriftVec2){cosf(a*2*(float)M_PI), sinf(a*2*(float)M_PI)};
	state->bodies.angular_velocity[body_idx] = 0; // TODO need better random spin?
	
	DriftFrame frame = DRIFT_FRAMES[DRIFT_PICKUP_ITEMS[type].frame];
	float radius = (frame.bounds.r - frame.bounds.l)/2, mass = 0.1f;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_ITEM;
	
	return e;
}

DriftSprite DriftSpriteForItem(DriftItemType type, DriftAffine matrix){
	DriftSpriteEnum frame = DRIFT_PICKUP_ITEMS[type].frame;
	return (DriftSprite){
		.frame = DRIFT_FRAMES[frame], .color = DRIFT_RGBA8_WHITE, .matrix = matrix,
		.shiny = (u8)(DRIFT_PICKUP_ITEMS[type].shiny/255),
	};
}

static DriftLight light_for_item(DriftItemType type, DriftAffine matrix){
	DriftSpriteEnum frame = DRIFT_PICKUP_ITEMS[type].light_frame;
	float size = DRIFT_PICKUP_ITEMS[type].light_size;
	return (DriftLight){
		.frame = DRIFT_FRAMES[frame], .color = DRIFT_PICKUP_ITEMS[type].light_color,
		.matrix = DriftAffineMul(matrix, (DriftAffine){size, 0, 0, size, 0, 0}),
	};
}

DriftEntity DriftItemMake(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	
	DriftEntity e = DRIFT_PICKUP_ITEMS[type].make(state, type, pos, vel);
	uint pickup_idx = DriftComponentAdd(&state->items.c, e);
	state->items.type[pickup_idx] = type;
	
	return e;
}

void DriftItemDraw(DriftDraw* draw, DriftItemType type, DriftVec2 pos){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	
	DriftAffine matrix = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteForItem(type, matrix));
	if(DRIFT_PICKUP_ITEMS[type].light_frame) DRIFT_ARRAY_PUSH(draw->lights, light_for_item(type, matrix));
	
	DriftItemDrawPickupFunc* func = DRIFT_PICKUP_ITEMS[type].draw;
	if(func) func(draw, pos);
}

void DriftItemGrab(DriftUpdate* update, DriftEntity entity, DriftItemType type){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	DriftComponentRemove(&update->state->items.c, entity);
	
	DriftItemPickupFunc* func = DRIFT_PICKUP_ITEMS[type].grab;
	if(func) func(update, entity);
}

void DriftItemDrop(DriftUpdate* update, DriftEntity entity, DriftItemType type){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	if(DriftComponentFind(&update->state->items.c, entity) == 0){
		uint pickup_idx = DriftComponentAdd(&update->state->items.c, entity);
		update->state->items.type[pickup_idx] = type;
	}
	
	DriftItemPickupFunc* func = DRIFT_PICKUP_ITEMS[type].drop;
	if(func) func(update, entity);
}

void DriftDrawItems(DriftDraw* draw){
	DriftGameState* state = draw->state;
	DriftAffine vp_matrix = draw->vp_matrix;
	
	uint item_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&item_idx, &state->items.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftVec2 pos = DriftAffineOrigin(m);
		DriftItemType type = state->items.type[item_idx];
		if(type == DRIFT_ITEM_POWER_NODE || !DriftAffineVisibility(vp_matrix, pos, (DriftVec2){16, 16})) continue;
		
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteForItem(type, m));
		if(DRIFT_PICKUP_ITEMS[type].light_frame) DRIFT_ARRAY_PUSH(draw->lights, light_for_item(type, m));
	}
}

void DriftTickItemSpawns(DriftUpdate* update){
	DriftGameState* state = update->state;

	DriftVec2 player_pos = ({
		uint body_idx = DriftComponentFind(&state->bodies.c, update->ctx->player);
		state->bodies.position[body_idx];
	});
	
	for(uint retries = 0; retries < 10; retries++){
		if(state->items.c.count >= 300) break;
		
		DriftVec2 pos = DriftVec2FMA(player_pos, DriftRandomInUnitCircle(), DRIFT_SPAWN_RADIUS);
		if(DriftCheckSpawn(update, pos, 4)){
			DriftItemType type = DRIFT_ITEM_NONE;
			DriftSelectionContext ctx = {.rand = rand()};
			switch(DriftTerrainSampleBiome(state->terra, pos).idx){
				case 0:{
					if(DriftSelectWeight(&ctx, 100)) type = DRIFT_ITEM_VIRIDIUM;
					// if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_LUMIUM;
					// if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_COPPER;
					// if(DriftSelectWeight(&ctx, 1)) type = DRIFT_ITEM_SCRAP;
				} break;
				
				// case 2:{
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_BORON;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_FLOURON;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_SILVER;
				// 	if(DriftSelectWeight(&ctx, 1)) type = DRIFT_ITEM_SCRAP;
				// } break;
				
				// case 1:{
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_RADONITE;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_FUNGICITE;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_GOLD;
				// 	if(DriftSelectWeight(&ctx, 1)) type = DRIFT_ITEM_ADVANCED_SCRAP;
				// } break;
				
				// case 3:{
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_METRIUM;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_MORPHITE;
				// 	if(DriftSelectWeight(&ctx, 5)) type = DRIFT_ITEM_GRAPHENE;
				// 	if(DriftSelectWeight(&ctx, 1)) type = DRIFT_ITEM_ADVANCED_SCRAP;
				// } break;
				
				// TODO
				default:{
					if(DriftSelectWeight(&ctx, 100)) type = DRIFT_ITEM_VIRIDIUM;
				} break;
			}
			DriftItemMake(state, type, pos, DRIFT_VEC2_ZERO);
		}
	}

	uint pickup_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->items.c, .variable = &pickup_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 delta = DriftVec2Sub(player_pos, state->bodies.position[body_idx]);
		if(DriftVec2Length(delta) > DRIFT_SPAWN_RADIUS){
			DriftDestroyEntity(update, join.entity);
		}
	}
}

#define UI_LINE_HEIGHT 10
#define STR_CORRUPT_DATA "C@RRUPT D&TA"

typedef struct {
	DriftDraw* draw;
	DriftGameState* state;
	mu_Context* mu;
	DriftItemType selected;
	bool focused;
} RowContext;

static void craft_item_row(RowContext* ctx, DriftItemType item){
	DriftGameState* state = ctx->state;
	
	const DriftCraftableItem* craftable = DRIFT_CRAFTABLES + item;
	DRIFT_ASSERT_WARN(craftable->ingredients[0].type, "Item is not craftable");
	
	DriftScanType scan_type = DRIFT_ITEMS[item].scan;
	
	const char* name = DRIFT_ITEMS[item].name;
	DRIFT_ASSERT_WARN(scan_type, "Scan type not set for '%s'.", name);
	bool is_recipe_known = state->scan_progress[scan_type] == 1;
	bool can_craft = !DRIFT_ITEMS[item].can_only_have_one || state->inventory[item] == 0;
	bool ingredients_known = true;
	
	const DriftIngredient* ingredients = craftable->ingredients;
	uint ingredient_count = 0, known_count = 0;
	while(ingredient_count < DRIFT_CRAFTABLE_MAX_INGREDIENTS){
		DriftItemType ingredient_type = ingredients[ingredient_count].type;
		if(ingredient_type == DRIFT_ITEM_NONE) break;
		
		uint have = state->inventory[ingredient_type], need = ingredients[ingredient_count].count;
		can_craft &= have >= need;
		
		bool ingredient_known = state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1;
		known_count += ingredient_known;
		ingredients_known &= ingredient_known;
		ingredient_count++;
	}
	
	
	mu_layout_row(ctx->mu, 1, (int[]){-1}, 16);
	bool gfocus = mu_begin_group(ctx->mu, 0);{
		if(mu_group_is_hovered(ctx->mu)){
			ctx->selected = item;
			ctx->focused = gfocus;
		}
		
		mu_layout_row(ctx->mu, 2, (int[]){-30, -1}, UI_LINE_HEIGHT);
		if(is_recipe_known){
			const char* color = can_craft ? DRIFT_TEXT_GRAY : DRIFT_TEXT_GRAY;
			mu_labelf(ctx->mu, "%s%s", color, name);
		} else {
			if(ingredients_known){
				mu_labelf(ctx->mu, "%s%s", DRIFT_TEXT_GREEN, name);
			} else {
				mu_label(ctx->mu, "{#404040FF}" STR_CORRUPT_DATA);
			}
			
			const char* color = (known_count == ingredient_count ? DRIFT_TEXT_GREEN : DRIFT_TEXT_RED);
			mu_labelf(ctx->mu, "%s%d/%d", color, known_count, ingredient_count);
		}
	} mu_end_group(ctx->mu);
}

static void craft_pane(RowContext* ctx){
	mu_Context* mu = ctx->mu;
	DriftGameState* state = ctx->state;
	
	DriftScanType scan_type = DRIFT_ITEMS[ctx->selected].scan;
	float* progress = state->scan_progress + scan_type;
	bool is_recipe_known = *progress == 1;
	bool can_only_have_one = DRIFT_ITEMS[ctx->selected].can_only_have_one;
	bool can_craft = is_recipe_known && (!can_only_have_one || state->inventory[ctx->selected] == 0);
	bool ingredients_known = true;
	
	const DriftCraftableItem* craftable = DRIFT_CRAFTABLES + ctx->selected;
	const DriftIngredient* ingredients = craftable->ingredients;
	uint ingredient_count = 0, known_count = 0;
	while(ingredient_count < DRIFT_CRAFTABLE_MAX_INGREDIENTS){
		DriftItemType ingredient_type = ingredients[ingredient_count].type;
		if(ingredient_type == DRIFT_ITEM_NONE) break;
		
		uint have = state->inventory[ingredient_type], need = ingredients[ingredient_count].count;
		bool have_enough = have >= need;
		bool ingredient_known = state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1;
		known_count += ingredient_known;
		can_craft &= have_enough && ingredient_known;
		ingredients_known &= ingredient_known;
		
		ingredient_count++;
	}
	
	const char* img_name = DRIFT_ITEMS[*progress == 1 ? ctx->selected : DRIFT_ITEM_NONE].name;
	static const char* LOADED;
	if(LOADED != img_name){
		LOADED = img_name;
		
		// const char* name = DRIFT_ITEMS[ctx->selected].name;
		char* filename = DriftSMPrintf(ctx->draw->mem, "gfx/scans/%s.qoi", img_name);
		
		// Normalize file name.
		for(char* c = filename; *c; c++){
			if('A' <= *c && *c <='Z') *c += 0x20;
			if(*c == ' ') *c = '_';
		}
		DRIFT_LOG("loading scan image '%s'", filename);
		
		DriftImage img = DriftAssetLoadImage(DriftSystemMem, filename);
		img.pixels = DriftRealloc(DriftSystemMem, img.pixels, img.w*img.h*4, DRIFT_ATLAS_SIZE*DRIFT_ATLAS_SIZE*4);
		
		const DriftGfxDriver* driver = ctx->draw->shared->driver;
		uint queue = tina_job_switch_queue(ctx->draw->job, DRIFT_JOB_QUEUE_GFX);
		driver->load_texture_layer(driver, ctx->draw->shared->atlas_texture, DRIFT_ATLAS_SCAN, img.pixels);
		tina_job_switch_queue(ctx->draw->job, queue);
	}
	
	mu_layout_row(mu, 1, (int[]){256}, 64); {
		mu_Rect r = mu_layout_next(mu);
		mu_draw_icon(mu, -DRIFT_SPRITE_SCAN_IMAGE, r, (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
	}
	
	mu_layout_row(mu, 2, (int[]){-90, -1}, 0);
	mu_layout_begin_column(mu); {
		int cols[] = {-70, -1};
		mu_layout_row(mu, 2, cols, UI_LINE_HEIGHT);
		const char* label = (ingredients_known ? DRIFT_ITEMS[ctx->selected].name : STR_CORRUPT_DATA);
		const char* color = (ingredients_known ? DRIFT_TEXT_WHITE : DRIFT_TEXT_RED);
		mu_labelf(mu, "%s%s", color, label);
		
		if(is_recipe_known){
			mu_labelf(mu, DRIFT_TEXT_GRAY"(have %2d)", state->inventory[ctx->selected]);
		} else {
			mu_labelf(mu, "    %d / %d", known_count, ingredient_count);
		}
		
		for(uint i = 0; i < ingredient_count; i++){
			uint ingredient_type = ingredients[i].type;
			const char* ingredient_name = DRIFT_ITEMS[ingredient_type].name;
			if(state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] < 1){
				mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
				mu_labelf(mu, DRIFT_TEXT_GRAY"- "DRIFT_TEXT_RED"UNKNOWN MATERIAL");
			} else {
				uint have = state->inventory[ingredient_type], need = ingredients[i].count;
				bool enough = have >= need;
				const char* color = enough ? DRIFT_TEXT_GREEN : DRIFT_TEXT_RED;
				mu_layout_row(mu, 2, cols, UI_LINE_HEIGHT);
				mu_labelf(mu, DRIFT_TEXT_GRAY"- %s", ingredient_name);
				
				if(is_recipe_known) mu_labelf(mu, "%s %3d / %2d", color, have, need);
			}
		}
	} mu_layout_end_column(mu);
	mu_layout_begin_column(mu); {
		mu_layout_row(mu, 1, (int[]){-1}, 0);
		if(can_craft){
			if(mu_button(mu, "Craft {@ACCEPT}") || ctx->focused){
				state->inventory[ctx->selected] += craftable->makes ?: 1;
				for(uint p = 0; p < ingredient_count; p++){
					state->inventory[ingredients[p].type] -= ingredients[p].count;
				}
			}
			if(craftable->makes) mu_labelf(mu, DRIFT_TEXT_GRAY"(Makes: %2d)", craftable->makes);
		}else if(known_count == ingredient_count && *progress == 0){
			if(mu_button(mu, "Research {@ACCEPT}") || ctx->focused) *progress = 0.1f;
		} else if(0 < *progress && *progress < 1){
			DRIFT_ASSERT(*progress > 0, "oops");
			mu_Rect r = mu_layout_next(mu);
			mu->draw_frame(mu, r, MU_COLOR_PROGRESSBASE);
			r.w = (int)(r.w*(*progress));
			mu->draw_frame(mu, r, MU_COLOR_BUTTON);
			*progress = fminf(1, *progress + ctx->draw->dt/2);
		}
	} mu_layout_end_column(mu);
	
	mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
	if(known_count < ingredient_count){
		mu_label(mu, "{#008080FF}Scan"DRIFT_TEXT_WHITE" or {#008080FF}research"DRIFT_TEXT_WHITE" to rediscover materials.");
	} else if(can_only_have_one && state->inventory[ctx->selected]){
		mu_label(mu, "You've built this upgrade already.");
	} else if(is_recipe_known && !can_craft){
		mu_label(mu, "Insufficient Resources to fabricate.");
	} else if(ingredients_known){
		mu_label(mu, "All materials found. Research to unlock.");
	}
}

static void craft_ui(DriftDraw* draw){
	mu_Context* mu = draw->ctx->mu;
	DriftGameState* state = draw->state;
	RowContext ctx = {.draw = draw, .mu = mu, .state = state, .selected = DRIFT_ITEM_NONE};
	mu_layout_row(mu, 2, (int[]){-260, -1}, -1);
	
	mu_begin_panel(mu, "list");
	if(draw->ctx->debug.show_ui){
		mu_layout_row(mu, 1, (int[]){-1}, 0);
		if(mu_button(mu, "cheaty cheater")){
			state->inventory[DRIFT_ITEM_VIRIDIUM] += 100;
			state->inventory[DRIFT_ITEM_LUMIUM] += 100;
			state->inventory[DRIFT_ITEM_SCRAP] += 100;
			state->inventory[DRIFT_ITEM_POWER_SUPPLY] += 100;
			state->inventory[DRIFT_ITEM_OPTICS] += 100;
			state->inventory[DRIFT_ITEM_POWER_NODE] += 100;
		}
	}
	
	mu_label(mu, "Parts:");
	craft_item_row(&ctx, DRIFT_ITEM_OPTICS);
	mu_label(mu, "Items:");
	craft_item_row(&ctx, DRIFT_ITEM_POWER_NODE);
	mu_label(mu, "Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_HEADLIGHT);
	craft_item_row(&ctx, DRIFT_ITEM_AUTOCANNON);
	craft_item_row(&ctx, DRIFT_ITEM_MINING_LASER);
	mu_end_panel(mu);
	
	mu_layout_begin_column(mu);
	craft_pane(&ctx);
	mu_layout_end_column(mu);
}

static const mu_Vec2 UI_SIZE = {400, 200};

static void reboot_ui(DriftDraw* draw){
	mu_Context* mu = draw->ctx->mu;
	DriftGameState* state = draw->state;
	RowContext ctx = {.draw = draw, .mu = mu, .state = state, .selected = DRIFT_ITEM_NONE};
	
	static bool show_crash = true;
	if(show_crash){
		static uint line_max = 0;
		static const char* crash_txt[] = {
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started System Logging Service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Create Volatile Files and Directories.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started LSB: automatic crash report generation.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started crash report submission.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started RPC bind portmap service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Reached target System Initialization.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started ACPI Events Check.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Reached target Path Units.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Initialize hardware monitoring sensors.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started Thermal Daemon Service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Raise network interfaces.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started IIO Sensor Proxy service.\n",
			DRIFT_TEXT_GRAY"["DRIFT_TEXT_RED"FAILED"DRIFT_TEXT_GRAY"] Initialize blueprint database.\n",
			DRIFT_TEXT_RED"         Device storage corrupt!",
			NULL,
		};
	
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_Rect r = mu_layout_next(mu);
		for(uint i = 0; i < line_max/6; i++){
			mu_draw_text(mu, mu->style->font, crash_txt[i], -1, (mu_Vec2){r.x, r.y}, (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
			r.y += 10;
		}
		
		if(crash_txt[line_max/6]){
			line_max++;
		} else {
			mu_Vec2 size = {160, 20};
			mu_layout_set_next(mu, (mu_Rect){(UI_SIZE.x - size.x)/2, (UI_SIZE.y - size.y - 8), size.x, size.y}, true);
			if(mu_button(mu, "Reboot Fabricator {@ACCEPT}") || mu->key_down & MU_KEY_RETURN) show_crash = false;
		}
	} else {
		static char* reboot_txt;
		if(reboot_txt == NULL){
			DriftData data = DriftAssetLoad(DriftSystemMem, "bin/reboot.txt");
			reboot_txt = DriftRealloc(DriftSystemMem, data.ptr, data.size, data.size + 1);
			reboot_txt[data.size] = 0;
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_begin_panel_ex(mu, "reboot", MU_OPT_NOSCROLL);
		static int scroll = 0;
		mu_Container* cnt = mu_get_current_container(mu);
		cnt->scroll = (mu_Vec2){0, scroll};
		scroll += (int)(600*draw->dt);
		if(scroll > 1200) state->status.factory_rebooted = true;
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_text(mu, reboot_txt);
		mu_end_panel(mu);
	}
}

void DriftCraftUI(DriftDraw* draw, DriftUIState* ui_state){
	mu_Context* mu = draw->ctx->mu;
	DriftVec2 extents = draw->internal_extent;
	
	static const char* TITLE = "Fabricator";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->rect = (mu_Rect){(int)(extents.x - UI_SIZE.x)/2, (int)(extents.y - UI_SIZE.y)/2, UI_SIZE.x, UI_SIZE.y};
	win->open = (*ui_state == DRIFT_UI_STATE_CRAFT);
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, MU_OPT_NOSCROLL)){
		mu_layout_row(mu, 2, (int[]){-1}, 18);
		mu_begin_box(mu, MU_COLOR_GROUPBG, 0);
		mu_layout_row(mu, 2, (int[]){-60, -1}, -1);
		mu_label(mu, "Fabricator:");
		if(mu_button(mu, "Close {@CANCEL}") || mu->key_down & MU_KEY_ESCAPE) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_box(mu);
		
		if(draw->state->status.factory_rebooted){
			craft_ui(draw);
		} else {
			reboot_ui(draw);
		}
		
		mu_check_close(mu);
		if(!win->open) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_window(mu);
	}
}
