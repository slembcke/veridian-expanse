#include "drift_game.h"

const DriftItem DRIFT_ITEMS[_DRIFT_ITEM_TYPE_COUNT] = {
	[DRIFT_ITEM_TYPE_NONE] = {.name = "<NONE>", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_SCRAP] = {.name = "Scrap", .icon = DRIFT_SPRITE_NO_GFX, .cargo = true},
	[DRIFT_ITEM_TYPE_ORE] = {.name = "Ore", .icon = DRIFT_SPRITE_ORE_CHUNK0, .cargo = true},
	[DRIFT_ITEM_TYPE_LUMIUM] = {.name = "Lumium", .icon = DRIFT_SPRITE_LUMIUM_CHUNK0, .cargo = true},
	[DRIFT_ITEM_TYPE_OPTICS] = {.name = "Optical Parts", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_POWER_SUPPLY] = {.name = "Power Supply", .icon = DRIFT_SPRITE_NO_GFX},
	// [DRIFT_ITEM_TYPE_CPU] = {.name = "CPU", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_POWER_NODE] = {.name = "Power Node", .icon = DRIFT_SPRITE_POWER_NODE, .cargo = true},
	[DRIFT_ITEM_TYPE_HEADLIGHT] = {.name = "Headlight", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_RUSTY_CANNON] = {.name = "Rusty Cannon", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_CANNON] = {.name = "Cannon", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_AUTOCANNON] = {.name = "Autocannon", .icon = DRIFT_SPRITE_NO_GFX},
	[DRIFT_ITEM_TYPE_MINING_LASER] = {.name = "Mining Laser", .icon = DRIFT_SPRITE_NO_GFX},
	// [DRIFT_ITEM_TYPE_SMELTING_MODULE] = {.name = "Smelting Module", .icon = DRIFT_SPRITE_NO_GFX},
	// [DRIFT_ITEM_TYPE_DRONE_CONTROLLER] = {.name = "Drone Controller", .icon = DRIFT_SPRITE_NO_GFX},
	// [DRIFT_ITEM_TYPE_DRONE] = {.name = "Drone", .icon = DRIFT_SPRITE_NO_GFX},
};

const DriftCraftableItem DRIFT_CRAFTABLES[_DRIFT_ITEM_TYPE_COUNT] = {
	[DRIFT_ITEM_TYPE_POWER_SUPPLY] = {{
		{.type = DRIFT_ITEM_TYPE_SCRAP, .count = 1},
	}},
	[DRIFT_ITEM_TYPE_OPTICS] = {{
		{.type = DRIFT_ITEM_TYPE_SCRAP, .count = 1},
	}},
	// [DRIFT_ITEM_TYPE_CPU] = {{
	// 	{.type = DRIFT_ITEM_TYPE_SCRAP, .count = 1},
	// }},
	[DRIFT_ITEM_TYPE_HEADLIGHT] = {{
		{.type = DRIFT_ITEM_TYPE_POWER_SUPPLY, .count = 1},
		{.type = DRIFT_ITEM_TYPE_OPTICS, .count = 1},
		{.type = DRIFT_ITEM_TYPE_LUMIUM, .count = 10},
	}},
	[DRIFT_ITEM_TYPE_POWER_NODE] = {{
		{.type = DRIFT_ITEM_TYPE_POWER_SUPPLY, .count = 1},
		{.type = DRIFT_ITEM_TYPE_LUMIUM, .count = 1},
	}},
	[DRIFT_ITEM_TYPE_RUSTY_CANNON] = {{
		{.type = DRIFT_ITEM_TYPE_ORE, .count = 5},
	}},
	[DRIFT_ITEM_TYPE_CANNON] = {{
		{.type = DRIFT_ITEM_TYPE_SCRAP, .count = 2},
		{.type = DRIFT_ITEM_TYPE_ORE, .count = 10},
	}},
	[DRIFT_ITEM_TYPE_AUTOCANNON] = {{
		{.type = DRIFT_ITEM_TYPE_SCRAP, .count = 4},
		{.type = DRIFT_ITEM_TYPE_ORE, .count = 20},
	}},
	[DRIFT_ITEM_TYPE_MINING_LASER] = {{
		{.type = DRIFT_ITEM_TYPE_POWER_SUPPLY, .count = 4},
		{.type = DRIFT_ITEM_TYPE_OPTICS, .count = 4},
		{.type = DRIFT_ITEM_TYPE_LUMIUM, .count = 20},
	}},
	// [DRIFT_ITEM_TYPE_SMELTING_MODULE] = {{
	// 	{.type = DRIFT_ITEM_TYPE_ORE, .count = 1},
	// }},
};
