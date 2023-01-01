typedef enum {
	DRIFT_ITEM_NONE,
	
	// Scrap materials
	DRIFT_ITEM_SCRAP,
	DRIFT_ITEM_ADVANCED_SCRAP,
	
	// Ore materials
	DRIFT_ITEM_VIRIDIUM,
	DRIFT_ITEM_BORONITE,
	DRIFT_ITEM_RADONITE,
	DRIFT_ITEM_METRIUM,
	
	// Rare materials
	DRIFT_ITEM_COPPER,
	DRIFT_ITEM_SILVER,
	DRIFT_ITEM_GOLD,
	DRIFT_ITEM_GRAPHENE,
	
	// Biomech materials
	DRIFT_ITEM_LUMIUM,
	DRIFT_ITEM_FLOURON,
	DRIFT_ITEM_FUNGICITE,
	DRIFT_ITEM_MORPHITE,
	
	// Intermediate materials
	DRIFT_ITEM_POWER_SUPPLY,
	DRIFT_ITEM_OPTICS,
	// DRIFT_ITEM_CPU,
	
	// Tools
	DRIFT_ITEM_HEADLIGHT,
	DRIFT_ITEM_CANNON,
	DRIFT_ITEM_MINING_LASER,
	DRIFT_ITEM_SCANNER,
	DRIFT_ITEM_DRONE_CONTROLLER,
	
	// Upgrades
	DRIFT_ITEM_AUTOCANNON,
	DRIFT_ITEM_ZIP_CANNON,
	DRIFT_ITEM_HEAT_EXCHANGER,
	DRIFT_ITEM_THERMAL_CONDENSER,
	DRIFT_ITEM_RADIO_PLATING,
	DRIFT_ITEM_MIRROR_PLATING,
	DRIFT_ITEM_SPECTROMETER,
	DRIFT_ITEM_SMELTING_MODULE,
	
	// Consumables
	DRIFT_ITEM_POWER_NODE,
	DRIFT_ITEM_FUNGAL_NODE,
	DRIFT_ITEM_METRIUM_NODE,
	// DRIFT_ITEM_VIRIDIUM_SLUG,
	// DRIFT_ITEM_MISSILES,
	DRIFT_ITEM_DRONE,
	
	_DRIFT_ITEM_COUNT,
} DriftItemType;

typedef struct {
	const char* name;
	DriftScanType scan;
	bool is_cargo;
	bool can_only_have_one;
} DriftItem;

extern const DriftItem DRIFT_ITEMS[_DRIFT_ITEM_COUNT];

typedef struct {
	uint type, count;
} DriftIngredient;

#define DRIFT_CRAFTABLE_MAX_INGREDIENTS 4

typedef struct {
	uint makes;
	DriftIngredient ingredients[DRIFT_CRAFTABLE_MAX_INGREDIENTS];
} DriftCraftableItem;

extern const DriftCraftableItem DRIFT_CRAFTABLES[_DRIFT_ITEM_COUNT];

DriftEntity DriftItemMake(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel);
void DriftItemDraw(DriftDraw* draw, DriftItemType type, DriftVec2 pos);
void DriftItemGrab(DriftUpdate* update, DriftEntity entity, DriftItemType type);
void DriftItemDrop(DriftUpdate* update, DriftEntity entity, DriftItemType type);

void DriftTickItemSpawns(DriftUpdate* update);
void DriftDrawItems(DriftDraw* draw);
DriftSprite DriftSpriteForItem(DriftItemType type, DriftAffine matrix);

void DriftPowerNodeActivate(DriftGameState* state, DriftEntity e, DriftMem* mem);
